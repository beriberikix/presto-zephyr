/*
 * ST7701 parallel-RGB (DPI) display driver for the Pimoroni Presto (RP2350B).
 *
 * The ST7701S panel has no on-controller GRAM: pixels are streamed
 * continuously over an 18bpp parallel-RGB bus (16 lanes used for RGB565)
 * with HSYNC/VSYNC/DE/PCLK timing. Two RP2350 PIO1 state machines do the
 * work - one emits pixel data, one generates the sync timing - fed by a pair
 * of DMA channels and two ISRs. The framebuffer lives in SRAM and is scanned
 * out forever; display_write() blits into it.
 *
 * This is a port of the MIT-licensed Pimoroni Presto firmware
 * (github.com/pimoroni/presto, drivers/st7701, by Michael Bell and
 * Phil Howard). The PIO programs, init sequence, scanout ISRs and DMA
 * configuration follow that reference closely; the Zephyr device-model glue
 * and the bit-banged command bus are new.
 *
 * Copyright (c) 2026 Jonathan Beri
 * Portions Copyright (c) 2024 Pimoroni Ltd
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT pimoroni_st7701_presto

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/misc/pio_rpi_pico/pio_rpi_pico.h>
#include <zephyr/logging/log.h>
#include <zephyr/toolchain.h>
#include <string.h>

#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>

#include "st7701_parallel.pio.h"
#include "st7701_timing.pio.h"

LOG_MODULE_REGISTER(st7701_presto, CONFIG_DISPLAY_LOG_LEVEL);

/*
 * System clock, taken from devicetree rather than the pico-sdk
 * clock_get_hz(clk_sys): Zephyr's clock_control driver programs the PLLs with
 * its own register writes and does not populate the pico-sdk clock state, so
 * clock_get_hz() is unreliable here. The clkdiv is derived from this.
 */
#define ST7701_SYS_CLK_HZ DT_PROP(DT_NODELABEL(clk_sys), clock_frequency)
BUILD_ASSERT(ST7701_SYS_CLK_HZ > 0, "clk_sys frequency unknown");

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
	     "Only a single ST7701 Presto panel is supported");

/* Panel timing (in pixel clocks / lines), from the Pimoroni reference. */
#define DISPLAY_HEIGHT   480
#define TIMING_V_PULSE   8
#define TIMING_V_BACK    (5 + TIMING_V_PULSE)               /* 13 */
#define TIMING_V_DISPLAY (DISPLAY_HEIGHT + TIMING_V_BACK)   /* 493 */
#define TIMING_V_FRONT   (5 + TIMING_V_DISPLAY)             /* 498 */
#define TIMING_H_FRONT   4
#define TIMING_H_PULSE   16
#define TIMING_H_BACK    30
#define TIMING_H_DISPLAY 480

/* Max PIO clock the parallel/timing programs tolerate (Pimoroni reference). */
#define ST7701_MAX_PIO_CLK_HZ (34u * 1000u * 1000u)

struct st7701_presto_config {
	const struct device *pio_dev;
	struct gpio_dt_spec cmd_clk;
	struct gpio_dt_spec cmd_data;
	struct gpio_dt_spec cmd_cs;
	struct gpio_dt_spec backlight;
	struct gpio_dt_spec reset;   /* optional: .port == NULL if absent */
	uint16_t width;
	uint16_t height;
	uint8_t d0;                  /* first parallel data GPIO */
	uint8_t data_count;          /* number of data lanes (16 for RGB565) */
	uint8_t hsync;
	uint8_t vsync;
	uint8_t de;
	uint8_t pclk;
};

struct st7701_presto_data {
	PIO pio;
	uint16_t *fb;       /* buffer currently scanned out (read by the ISRs) */
	uint16_t *fb_back;  /* buffer the application renders into */
	uint16_t width;
	uint16_t height;
	size_t parallel_sm;
	size_t timing_sm;
	uint parallel_offset;
	int dma_data;
	int dma_ctrl;

	/* Scanout state, mutated by the ISRs. */
	volatile uint16_t timing_row;
	volatile uint16_t timing_phase;
	volatile int display_row;
	volatile uint16_t *next_line_addr;
	volatile bool flip_pending; /* swap fb/fb_back at the next vertical blank */
};

/*
 * Physical panel geometry, from devicetree. The scanout always drives this
 * many columns/lines (and the timing constants above match it).
 */
#define PANEL_WIDTH  DT_INST_PROP(0, width)
#define PANEL_HEIGHT DT_INST_PROP(0, height)

BUILD_ASSERT(PANEL_WIDTH == TIMING_H_DISPLAY, "panel width must match the timing program");
BUILD_ASSERT(PANEL_HEIGHT == DISPLAY_HEIGHT, "panel height must match the timing program");

/*
 * Framebuffer geometry. Full-res is one source pixel per panel pixel. Half-res
 * (CONFIG_ST7701_PRESTO_HALF_RES) drives the panel from a quarter-size 240x240
 * buffer that is pixel- and line-doubled during scanout: the parallel SM runs
 * at half the PCLK rate (each source pixel spans two panel columns) and each
 * source line is fetched twice (FB_ROW_SHIFT). 480x480 RGB565 = 450 KB;
 * 240x240 = 115 KB, leaving SRAM for a concurrent networking stack.
 */
#if defined(CONFIG_ST7701_PRESTO_HALF_RES)
#define FB_WIDTH     (PANEL_WIDTH / 2)
#define FB_HEIGHT    (PANEL_HEIGHT / 2)
#define FB_ROW_SHIFT 1
#else
#define FB_WIDTH     PANEL_WIDTH
#define FB_HEIGHT    PANEL_HEIGHT
#define FB_ROW_SHIFT 0
#endif

/*
 * Framebuffer(s) in SRAM. Uninitialised (cleared at probe). With
 * CONFIG_ST7701_PRESTO_DOUBLE_BUFFER there are two: one is scanned out while the
 * application renders into the other, swapped at the vertical blank by
 * st7701_presto_flip() (tear-free). Two full-res buffers do not fit in SRAM, so
 * double-buffering is practical only with CONFIG_ST7701_PRESTO_HALF_RES - the
 * BUILD_ASSERT below enforces that.
 */
#if defined(CONFIG_ST7701_PRESTO_DOUBLE_BUFFER)
#define FB_BUFFERS 2
#else
#define FB_BUFFERS 1
#endif

static uint16_t st7701_fb[FB_BUFFERS][FB_WIDTH * FB_HEIGHT] __noinit __aligned(4);

BUILD_ASSERT(DT_INST_PROP(0, data_pin_count) == 16,
	     "RGB565 scanout drives exactly 16 data lanes");
BUILD_ASSERT((FB_WIDTH & 1) == 0, "width must be even (DMA packs 2 px per word)");
BUILD_ASSERT(sizeof(st7701_fb) <= (size_t)CONFIG_SRAM_SIZE * 1024,
	     "framebuffer(s) do not fit in SRAM (try CONFIG_ST7701_PRESTO_HALF_RES)");

/* Single global instance, referenced by the (parameter-less) ISRs. */
static struct st7701_presto_data *st7701_isr_data;

#if defined(CONFIG_ST7701_PRESTO_DOUBLE_BUFFER)
/* Given by the end-of-frame ISR once a requested buffer swap has taken effect. */
static K_SEM_DEFINE(st7701_flip_sem, 0, 1);
#endif

/* ------------------------------------------------------------------ */
/* Scanout ISRs (ported from ST7701::drive_timing / handle_end_of_line) */
/* ------------------------------------------------------------------ */

/*
 * Feed the timing SM's TX FIFO. Each display line is described by four words
 * (front porch / hsync / back porch / display). The word layout consumed by
 * st7701_timing.pio is: [31]=VSYNC [30]=HSYNC [29:16]=delay-3 [15:0]=instr.
 */
static void __ramfunc st7701_drive_timing(struct st7701_presto_data *data)
{
	PIO pio = data->pio;

	while (!pio_sm_is_tx_fifo_full(pio, data->timing_sm)) {
		uint32_t instr;

		switch (data->timing_phase) {
		case 0: /* Front porch */
			instr = 0x4000B042u; /* HSYNC high, NOP */
			if (data->timing_row >= TIMING_V_PULSE) {
				instr |= 0x80000000u; /* VSYNC high */
			}
			instr |= (uint32_t)(TIMING_H_FRONT - 3) << 16;
			pio_sm_put(pio, data->timing_sm, instr);
			break;
		case 1: /* HSYNC pulse */
			instr = 0x0000B042u; /* HSYNC low, NOP */
			if (data->timing_row >= TIMING_V_PULSE) {
				instr |= 0x80000000u;
			}
			instr |= (uint32_t)(TIMING_H_PULSE - 3) << 16;
			pio_sm_put(pio, data->timing_sm, instr);
			break;
		case 2: /* Back porch; trigger the data SM inside the display window */
			instr = 0x40000000u; /* HSYNC high */
			if (data->timing_row >= TIMING_V_PULSE) {
				instr |= 0x80000000u;
			}
			if (data->timing_row >= TIMING_V_BACK &&
			    data->timing_row < TIMING_V_DISPLAY) {
				instr |= 0xD004u; /* irq 4 -> release the parallel SM */
			} else {
				instr |= 0xB042u; /* NOP */
			}
			instr |= (uint32_t)(TIMING_H_BACK - 3) << 16;
			pio_sm_put(pio, data->timing_sm, instr);
			break;
		case 3: /* Active display */
			instr = 0x40000000u; /* HSYNC high */
			if (data->timing_row == TIMING_V_DISPLAY) {
				instr |= 0xD001u; /* irq 1 -> end-of-frame */
			} else if (data->timing_row >= TIMING_V_BACK - 1 &&
				   data->timing_row < TIMING_V_DISPLAY) {
				instr |= 0xD000u; /* irq 0 -> end-of-line */
			} else {
				instr |= 0xB042u; /* NOP */
			}
			if (data->timing_row >= TIMING_V_PULSE) {
				instr |= 0x80000000u;
			}
			instr |= (uint32_t)(TIMING_H_DISPLAY - 3) << 16;
			pio_sm_put(pio, data->timing_sm, instr);

			if (++data->timing_row >= TIMING_V_FRONT) {
				data->timing_row = 0;
			}
			break;
		}

		data->timing_phase = (data->timing_phase + 1) & 3;
	}
}

static void __ramfunc st7701_timing_isr(const void *arg)
{
	ARG_UNUSED(arg);
	st7701_drive_timing(st7701_isr_data);
}

/* Advance the per-line DMA source address. display_row counts panel scanlines
 * (always PANEL_HEIGHT of them); the source row is display_row >> FB_ROW_SHIFT,
 * so in half-res each source line feeds two consecutive panel lines.
 */
static void __ramfunc st7701_start_line_xfer(struct st7701_presto_data *data)
{
	hw_clear_bits(&data->pio->irq, 0x1u);

	if (++data->display_row >= PANEL_HEIGHT) {
		data->next_line_addr = NULL;
	} else {
		data->next_line_addr =
			&data->fb[(size_t)data->width * (data->display_row >> FB_ROW_SHIFT)];
	}
}

/* Restart the data path for a fresh frame (in the vertical blank). */
static void __ramfunc st7701_start_frame_xfer(struct st7701_presto_data *data)
{
	PIO pio = data->pio;

	hw_clear_bits(&pio->irq, 0x2u);

#if defined(CONFIG_ST7701_PRESTO_DOUBLE_BUFFER)
	/* Present a pending flip here, between frames, so the swap is tear-free. */
	if (data->flip_pending) {
		uint16_t *tmp = data->fb;

		data->fb = data->fb_back;
		data->fb_back = tmp;
		data->flip_pending = false;
		k_sem_give(&st7701_flip_sem);
	}
#endif

	data->next_line_addr = NULL;
	dma_channel_abort(data->dma_data);
	dma_channel_wait_for_finish_blocking(data->dma_data);

	pio_sm_set_enabled(pio, data->parallel_sm, false);
	pio_sm_clear_fifos(pio, data->parallel_sm);
	pio_sm_exec_wait_blocking(pio, data->parallel_sm, pio_encode_mov(pio_osr, pio_null));
	pio_sm_exec_wait_blocking(pio, data->parallel_sm, pio_encode_out(pio_null, 32));
	pio_sm_exec_wait_blocking(pio, data->parallel_sm, pio_encode_jmp(data->parallel_offset));
	pio_sm_set_enabled(pio, data->parallel_sm, true);

	data->display_row = 0;
	data->next_line_addr = data->fb;
	dma_channel_set_read_addr(data->dma_data, data->fb, true);
}

static void __ramfunc st7701_eol_isr(const void *arg)
{
	struct st7701_presto_data *data = st7701_isr_data;

	ARG_UNUSED(arg);

	if (data->pio->irq & 0x2u) {
		st7701_start_frame_xfer(data);
	} else {
		st7701_start_line_xfer(data);
	}
}

/* ------------------------------------------------------------------ */
/* 9-bit command bus (bit-banged; D/CX is the 9th, most-significant bit) */
/* ------------------------------------------------------------------ */

static void st7701_clock9(const struct st7701_presto_config *cfg, bool dc, uint8_t val)
{
	/* MSB first: D/CX bit, then 8 data bits. SPI mode 0 (idle low, latch on
	 * the rising edge): set data, clock high, clock low.
	 */
	gpio_pin_set_dt(&cfg->cmd_data, dc ? 1 : 0);
	gpio_pin_set_dt(&cfg->cmd_clk, 1);
	gpio_pin_set_dt(&cfg->cmd_clk, 0);

	for (int i = 7; i >= 0; i--) {
		gpio_pin_set_dt(&cfg->cmd_data, (val >> i) & 1);
		gpio_pin_set_dt(&cfg->cmd_clk, 1);
		gpio_pin_set_dt(&cfg->cmd_clk, 0);
	}
}

static void st7701_command(const struct st7701_presto_config *cfg, uint8_t cmd,
			   size_t len, const uint8_t *data)
{
	gpio_pin_set_dt(&cfg->cmd_cs, 1); /* assert (active low) */
	st7701_clock9(cfg, false, cmd);
	for (size_t i = 0; i < len; i++) {
		st7701_clock9(cfg, true, data[i]);
	}
	gpio_pin_set_dt(&cfg->cmd_cs, 0); /* deassert */
}

#define ST7701_CMD(cfg, c, ...)                                                          \
	do {                                                                             \
		static const uint8_t _d[] = {__VA_ARGS__};                               \
		st7701_command((cfg), (c), sizeof(_d), _d);                              \
	} while (0)

static void st7701_panel_init_seq(const struct st7701_presto_config *cfg)
{
	st7701_command(cfg, 0x01, 0, NULL); /* SWRESET */
	k_msleep(150);

	/* Command2 BK0 */
	ST7701_CMD(cfg, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x10);
	ST7701_CMD(cfg, 0x36, 0x00);                       /* MADCTL: RGB, normal scan */
	ST7701_CMD(cfg, 0xC0, 0x3b, 0x00);                 /* LNESET: 480 lines */
	ST7701_CMD(cfg, 0xC1, 0x0d, 0x02);                 /* PORCTRL: 13 VBP, 2 VFP */
	ST7701_CMD(cfg, 0xC2, 0x31, 0x01);                 /* INVSET */
	ST7701_CMD(cfg, 0xCD, 0x08);                       /* COLCTRL */
	ST7701_CMD(cfg, 0xB0, 0x00, 0x11, 0x18, 0x0e, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22,
		   0x04, 0x12, 0x0f, 0xaa, 0x31, 0x18); /* PVGAMCTRL */
	ST7701_CMD(cfg, 0xB1, 0x00, 0x11, 0x19, 0x0e, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22,
		   0x04, 0x11, 0x11, 0xa9, 0x32, 0x18); /* NVGAMCTRL */
	ST7701_CMD(cfg, 0xC3, 0x80, 0x2e, 0x0e);           /* RGBCTRL: HV mode, porch+sync */

	/* Command2 BK1 */
	ST7701_CMD(cfg, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x11);
	ST7701_CMD(cfg, 0xB0, 0x60); /* VHRS  4.7375V */
	ST7701_CMD(cfg, 0xB1, 0x32); /* VCOMS 0.725V */
	ST7701_CMD(cfg, 0xB2, 0x07); /* VGHSS 15V */
	ST7701_CMD(cfg, 0xB3, 0x80); /* TESTCMD */
	ST7701_CMD(cfg, 0xB5, 0x49); /* VGLS -10.17V */
	ST7701_CMD(cfg, 0xB7, 0x85); /* PWCTRL1 */
	ST7701_CMD(cfg, 0xB8, 0x21); /* PWCTRL2 */
	ST7701_CMD(cfg, 0xC1, 0x78); /* PDR1 */
	ST7701_CMD(cfg, 0xC2, 0x78); /* PDR2 */

	/* Panel-specific tuning (undocumented; from the Pimoroni reference). */
	ST7701_CMD(cfg, 0xE0, 0x00, 0x1b, 0x02);
	ST7701_CMD(cfg, 0xE1, 0x08, 0xa0, 0x00, 0x00, 0x07, 0xa0, 0x00, 0x00, 0x00, 0x44, 0x44);
	ST7701_CMD(cfg, 0xE2, 0x11, 0x11, 0x44, 0x44, 0xed, 0xa0, 0x00, 0x00, 0xec, 0xa0, 0x00, 0x00);
	ST7701_CMD(cfg, 0xE3, 0x00, 0x00, 0x11, 0x11);
	ST7701_CMD(cfg, 0xE4, 0x44, 0x44);
	ST7701_CMD(cfg, 0xE5, 0x0a, 0xe9, 0xd8, 0xa0, 0x0c, 0xeb, 0xd8, 0xa0, 0x0e, 0xed, 0xd8,
		   0xa0, 0x10, 0xef, 0xd8, 0xa0);
	ST7701_CMD(cfg, 0xE6, 0x00, 0x00, 0x11, 0x11);
	ST7701_CMD(cfg, 0xE7, 0x44, 0x44);
	ST7701_CMD(cfg, 0xE8, 0x09, 0xe8, 0xd8, 0xa0, 0x0b, 0xea, 0xd8, 0xa0, 0x0d, 0xec, 0xd8,
		   0xa0, 0x0f, 0xee, 0xd8, 0xa0);
	ST7701_CMD(cfg, 0xEB, 0x02, 0x00, 0xe4, 0xe4, 0x88, 0x00, 0x40);
	ST7701_CMD(cfg, 0xEC, 0x3c, 0x00);
	ST7701_CMD(cfg, 0xED, 0xab, 0x89, 0x76, 0x54, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		   0x20, 0x45, 0x67, 0x98, 0xba);
	ST7701_CMD(cfg, 0x36, 0x00);

	/* Command2 BK3 */
	ST7701_CMD(cfg, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x13);
	ST7701_CMD(cfg, 0xE5, 0xe4);

	/* Back to the user command set */
	ST7701_CMD(cfg, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00);
	ST7701_CMD(cfg, 0x3A, 0x66); /* COLMOD: 18bpp (16 lanes wired, 2 LSBs read 0) */
	st7701_command(cfg, 0x21, 0, NULL); /* INVON */
	k_msleep(1);
	st7701_command(cfg, 0x11, 0, NULL); /* SLPOUT */
	k_msleep(120);
	st7701_command(cfg, 0x29, 0, NULL); /* DISPON */
	k_msleep(50);
}

/* ------------------------------------------------------------------ */
/* PIO + DMA bring-up (ported from ST7701::init)                        */
/* ------------------------------------------------------------------ */

static int st7701_pio_dma_init(const struct st7701_presto_config *cfg,
			       struct st7701_presto_data *data)
{
	PIO pio = data->pio;
	uint timing_offset;
	pio_sm_config c;
	uint32_t sys_clk_hz, clk_div;
	size_t sm;

	if (pio_rpi_pico_allocate_sm(cfg->pio_dev, &sm) < 0) {
		return -EBUSY;
	}
	data->parallel_sm = sm;
	if (pio_rpi_pico_allocate_sm(cfg->pio_dev, &sm) < 0) {
		return -EBUSY;
	}
	data->timing_sm = sm;

	/* Timing program has .origin 0, so load it first into empty memory. */
	timing_offset = pio_add_program(pio, &st7701_timing_program);
	data->parallel_offset = pio_add_program(pio, &st7701_parallel_program);

	pio_gpio_init(pio, cfg->hsync);
	pio_gpio_init(pio, cfg->vsync);
	pio_gpio_init(pio, cfg->de);
	pio_gpio_init(pio, cfg->pclk);
	for (uint i = 0; i < cfg->data_count; i++) {
		pio_gpio_init(pio, cfg->d0 + i);
	}
	/* The two unused RGB666 lanes are held low as plain GPIOs. */
	for (uint i = cfg->data_count; i < 18; i++) {
		gpio_init(cfg->d0 + i);
		gpio_set_dir(cfg->d0 + i, GPIO_OUT);
		gpio_put(cfg->d0 + i, false);
	}

	pio_sm_set_consecutive_pindirs(pio, data->parallel_sm, cfg->d0, cfg->data_count, true);
	pio_sm_set_consecutive_pindirs(pio, data->parallel_sm, cfg->hsync, 4, true);

	sys_clk_hz = ST7701_SYS_CLK_HZ;
	clk_div = (sys_clk_hz + ST7701_MAX_PIO_CLK_HZ - 1) / ST7701_MAX_PIO_CLK_HZ;
	if (clk_div & 1) {
		clk_div += 1; /* keep even: parallel SM runs at half the timing rate */
	}

	/* Parallel (pixel data) SM. */
	c = st7701_parallel_program_get_default_config(data->parallel_offset);
	sm_config_set_out_pins(&c, cfg->d0, cfg->data_count);
	sm_config_set_sideset_pins(&c, cfg->de);
	sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
	sm_config_set_out_shift(&c, true, true, 32);
	sm_config_set_in_shift(&c, false, false, 32);
#if defined(CONFIG_ST7701_PRESTO_HALF_RES)
	/* Run the data SM at the PCLK rate: each of the 240 source pixels is held
	 * for two panel clocks, doubling it horizontally across two columns.
	 */
	sm_config_set_clkdiv(&c, (float)clk_div);
#else
	/* Full res: data SM at twice the PCLK rate -> one new pixel per PCLK. */
	sm_config_set_clkdiv(&c, (float)(clk_div >> 1));
#endif
	pio_sm_init(pio, data->parallel_sm, data->parallel_offset, &c);
	pio_sm_exec(pio, data->parallel_sm, pio_encode_out(pio_y, 32));
	pio_sm_put(pio, data->parallel_sm, (FB_WIDTH >> 1) - 1);
	pio_sm_set_enabled(pio, data->parallel_sm, true);

	/* Timing (sync generation) SM. */
	c = st7701_timing_program_get_default_config(timing_offset);
	sm_config_set_out_pins(&c, cfg->hsync, 2);
	sm_config_set_sideset_pins(&c, cfg->pclk);
	sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
	sm_config_set_out_shift(&c, false, true, 32);
	sm_config_set_clkdiv(&c, (float)clk_div);
	pio_sm_init(pio, data->timing_sm, timing_offset, &c);
	pio_sm_set_enabled(pio, data->timing_sm, true);

	/* DMA: data channel feeds the parallel TX FIFO one line at a time;
	 * control channel reloads the data channel's read address (+restart)
	 * from next_line_addr, forming a per-line ring.
	 */
	data->dma_data = dma_claim_unused_channel(true);
	data->dma_ctrl = dma_claim_unused_channel(true);

	dma_channel_config dc = dma_channel_get_default_config(data->dma_data);
	channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
	channel_config_set_dreq(&dc, pio_get_dreq(pio, data->parallel_sm, true));
	channel_config_set_bswap(&dc, true); /* orient RGB565 halfwords for ::isr */
	channel_config_set_chain_to(&dc, data->dma_ctrl);
	dma_channel_configure(data->dma_data, &dc, &pio->txf[data->parallel_sm], NULL,
			      FB_WIDTH >> 1, false);

	dc = dma_channel_get_default_config(data->dma_ctrl);
	channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
	channel_config_set_read_increment(&dc, false);
	dma_channel_configure(data->dma_ctrl, &dc,
			      &dma_hw->ch[data->dma_data].al3_read_addr_trig,
			      (void *)&data->next_line_addr, 1, false);

	return 0;
}

/* ------------------------------------------------------------------ */
/* display_driver_api                                                   */
/* ------------------------------------------------------------------ */

static int st7701_write(const struct device *dev, uint16_t x, uint16_t y,
			const struct display_buffer_descriptor *desc, const void *buf)
{
	struct st7701_presto_data *data = dev->data;
	const uint8_t *src = buf;
	uint16_t pitch = desc->pitch ? desc->pitch : desc->width;

	if ((uint32_t)x + desc->width > data->width ||
	    (uint32_t)y + desc->height > data->height) {
		return -EINVAL;
	}

	for (uint16_t row = 0; row < desc->height; row++) {
		const uint8_t *s = &src[(size_t)row * pitch * sizeof(uint16_t)];
		uint16_t *d = &data->fb_back[(size_t)(y + row) * data->width + x];

		/*
		 * Incoming pixels are standard little-endian RGB565
		 * (PIXEL_FORMAT_RGB_565). The Presto's PIO/DMA scanout consumes
		 * byte-swapped (big-endian) halfwords - the same framebuffer
		 * convention Pimoroni's PicoGraphics uses - so swap each pixel's
		 * bytes on the way in. Without this the bit-reversing parallel
		 * PIO program drives the colour fields onto the wrong data lanes
		 * (R->blue, G->red, B->green).
		 */
		for (uint16_t col = 0; col < desc->width; col++) {
			d[col] = (uint16_t)((s[2 * col] << 8) | s[2 * col + 1]);
		}
	}

	return 0;
}

static void st7701_get_capabilities(const struct device *dev,
				    struct display_capabilities *caps)
{
	struct st7701_presto_data *data = dev->data;

	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = data->width;
	caps->y_resolution = data->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
	caps->current_pixel_format = PIXEL_FORMAT_RGB_565;
	caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int st7701_set_pixel_format(const struct device *dev,
				   const enum display_pixel_format pf)
{
	ARG_UNUSED(dev);
	return (pf == PIXEL_FORMAT_RGB_565) ? 0 : -ENOTSUP;
}

static int st7701_set_brightness(const struct device *dev, uint8_t brightness)
{
	const struct st7701_presto_config *cfg = dev->config;

	return gpio_pin_set_dt(&cfg->backlight, brightness ? 1 : 0);
}

static int st7701_blanking_on(const struct device *dev)
{
	const struct st7701_presto_config *cfg = dev->config;

	return gpio_pin_set_dt(&cfg->backlight, 0);
}

static int st7701_blanking_off(const struct device *dev)
{
	const struct st7701_presto_config *cfg = dev->config;

	return gpio_pin_set_dt(&cfg->backlight, 1);
}

/*
 * Direct access to the back framebuffer (the one the app renders into; in a
 * single-buffer build this is also the scanout buffer). WARNING: unlike
 * display_write(), which accepts standard little-endian PIXEL_FORMAT_RGB_565,
 * the raw buffer holds *byte-swapped* (big-endian) RGB565 - the layout the
 * PIO/DMA scanout consumes (see st7701_write()). Callers writing pixels here
 * must byteswap each halfword themselves (e.g. sys_cpu_to_be16()). After a
 * st7701_presto_flip() the back buffer changes, so re-fetch it.
 */
static void *st7701_get_framebuffer(const struct device *dev)
{
	struct st7701_presto_data *data = dev->data;

	return data->fb_back;
}

/*
 * Present the rendered back buffer: request a swap at the next vertical blank
 * and block until it takes effect (tear-free). In a single-buffer build there
 * is nothing to swap and this returns immediately. Declare it where you call it
 * with: extern void st7701_presto_flip(const struct device *dev);
 */
void st7701_presto_flip(const struct device *dev)
{
#if defined(CONFIG_ST7701_PRESTO_DOUBLE_BUFFER)
	struct st7701_presto_data *data = dev->data;

	k_sem_reset(&st7701_flip_sem);
	data->flip_pending = true;
	k_sem_take(&st7701_flip_sem, K_FOREVER);
#else
	ARG_UNUSED(dev);
#endif
}

static const struct display_driver_api st7701_api = {
	.blanking_on = st7701_blanking_on,
	.blanking_off = st7701_blanking_off,
	.write = st7701_write,
	.get_framebuffer = st7701_get_framebuffer,
	.set_brightness = st7701_set_brightness,
	.get_capabilities = st7701_get_capabilities,
	.set_pixel_format = st7701_set_pixel_format,
};

/* ------------------------------------------------------------------ */
/* Init                                                                 */
/* ------------------------------------------------------------------ */

static int st7701_init(const struct device *dev)
{
	const struct st7701_presto_config *cfg = dev->config;
	struct st7701_presto_data *data = dev->data;
	int ret;

	if (!device_is_ready(cfg->pio_dev)) {
		LOG_ERR("PIO device not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&cfg->cmd_clk) || !gpio_is_ready_dt(&cfg->cmd_data) ||
	    !gpio_is_ready_dt(&cfg->cmd_cs) || !gpio_is_ready_dt(&cfg->backlight)) {
		LOG_ERR("command-bus / backlight GPIO not ready");
		return -ENODEV;
	}

	data->pio = pio_rpi_pico_get_pio(cfg->pio_dev);
	data->fb = st7701_fb[0];                  /* scanned out first */
	data->fb_back = st7701_fb[FB_BUFFERS - 1]; /* rendered into (== fb if single) */
	data->width = FB_WIDTH;     /* source (framebuffer) geometry: equals the */
	data->height = FB_HEIGHT;   /* panel in full-res, half it in half-res */
	st7701_isr_data = data;

	/* Command bus idle, backlight off. */
	gpio_pin_configure_dt(&cfg->cmd_clk, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&cfg->cmd_data, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&cfg->cmd_cs, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&cfg->backlight, GPIO_OUTPUT_INACTIVE);
	if (cfg->reset.port != NULL) {
		gpio_pin_configure_dt(&cfg->reset, GPIO_OUTPUT_ACTIVE);
		k_msleep(10);
		gpio_pin_set_dt(&cfg->reset, 0);
		k_msleep(10);
	}

	memset(st7701_fb, 0, sizeof(st7701_fb)); /* clear all buffers */

	ret = st7701_pio_dma_init(cfg, data);
	if (ret < 0) {
		LOG_ERR("PIO/DMA init failed (%d)", ret);
		return ret;
	}

	/* Send the panel init sequence over the command bus. */
	st7701_panel_init_seq(cfg);

	/* Wire up the scanout interrupts. The timing SM's TX-not-full drives
	 * irq line 1 (refills timing words); the SM IRQ flags 0/1 drive line 0
	 * (end-of-line / end-of-frame).
	 */
	hw_set_bits(&data->pio->inte1, 0x010u << data->timing_sm);
	IRQ_CONNECT(DT_IRQ_BY_NAME(DT_INST_PARENT(0), irq1, irq),
		    DT_IRQ_BY_NAME(DT_INST_PARENT(0), irq1, priority),
		    st7701_timing_isr, NULL, 0);
	irq_enable(DT_IRQ_BY_NAME(DT_INST_PARENT(0), irq1, irq));

	hw_set_bits(&data->pio->inte0, 0x300u);
	IRQ_CONNECT(DT_IRQ_BY_NAME(DT_INST_PARENT(0), irq0, irq),
		    DT_IRQ_BY_NAME(DT_INST_PARENT(0), irq0, priority),
		    st7701_eol_isr, NULL, 0);
	irq_enable(DT_IRQ_BY_NAME(DT_INST_PARENT(0), irq0, irq));

	/* Scanout is running; enable the backlight. */
	k_msleep(50);
	gpio_pin_set_dt(&cfg->backlight, 1);

	LOG_INF("ST7701 Presto display ready (%ux%u RGB565 -> %ux%u panel)",
		data->width, data->height, PANEL_WIDTH, PANEL_HEIGHT);
	return 0;
}

static struct st7701_presto_data st7701_data;

static const struct st7701_presto_config st7701_config = {
	.pio_dev = DEVICE_DT_GET(DT_INST_PARENT(0)),
	.cmd_clk = GPIO_DT_SPEC_INST_GET(0, cmd_clk_gpios),
	.cmd_data = GPIO_DT_SPEC_INST_GET(0, cmd_data_gpios),
	.cmd_cs = GPIO_DT_SPEC_INST_GET(0, cmd_cs_gpios),
	.backlight = GPIO_DT_SPEC_INST_GET(0, backlight_gpios),
	.reset = GPIO_DT_SPEC_INST_GET_OR(0, reset_gpios, {0}),
	.width = DT_INST_PROP(0, width),
	.height = DT_INST_PROP(0, height),
	.d0 = DT_INST_PROP(0, data_pin_base),
	.data_count = DT_INST_PROP(0, data_pin_count),
	.hsync = DT_INST_PROP(0, hsync_pin),
	.vsync = DT_INST_PROP(0, vsync_pin),
	.de = DT_INST_PROP(0, de_pin),
	.pclk = DT_INST_PROP(0, pclk_pin),
};

DEVICE_DT_INST_DEFINE(0, st7701_init, NULL, &st7701_data, &st7701_config,
		      POST_KERNEL, CONFIG_ST7701_PRESTO_INIT_PRIORITY, &st7701_api);
