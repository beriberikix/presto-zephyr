/*
 * APS6404 PSRAM bring-up for the Pimoroni Presto (RP2350B), QMI window 1.
 *
 * Maps the 8 MB PSRAM on the dedicated chip-select (GP47 = QMI CS1) into XIP
 * space at 0x11000000 so it is usable as plain RAM. Upstream Zephyr has no
 * RP2350 PSRAM/QMI driver and the pico-sdk exposes no high-level routine, so
 * the QMI window-1 init sequence is ported from the MIT-licensed MicroPython
 * rp2 port (ports/rp2/rp2_psram.c, (c) 2025 Phil Howard / Mike Bell /
 * Kirk D. Benell), which is what Pimoroni's firmware uses on this board.
 *
 * The QMI is the same peripheral that serves flash XIP on CS0, so the
 * direct-mode sequence (which stalls flash fetches) runs from SRAM
 * (__ramfunc) with interrupts disabled.
 *
 * Copyright (c) 2026 Jonathan Beri
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT pimoroni_rp2350_psram

#include <zephyr/init.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/toolchain.h>

#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip_ctrl.h>
#include <hardware/regs/qmi.h>

LOG_MODULE_REGISTER(psram_aps6404, CONFIG_PIMORONI_RP2350_PSRAM_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1, "only one PSRAM instance supported");

#define PSRAM_BASE    DT_INST_REG_ADDR(0)
#define PSRAM_DT_SIZE DT_INST_REG_SIZE(0)
#define PSRAM_CS_PIN  DT_INST_PROP(0, cs_pin)

/*
 * clk_sys feeds the QMI; derive the timing from it. Use the devicetree
 * clk_sys frequency rather than pico-sdk's clock_get_hz(clk_sys): Zephyr's
 * clock_control programs the PLLs itself, so the pico-sdk clock state is not
 * reliably populated (same reason the ST7701 driver uses the DT value).
 */
#define PSRAM_SYS_CLK_HZ DT_PROP(DT_NODELABEL(clk_sys), clock_frequency)
BUILD_ASSERT(PSRAM_SYS_CLK_HZ > 0, "clk_sys frequency unknown");

/* APS6404 max QSPI clock. */
#define APS6404_MAX_FREQ_HZ 133000000

static size_t psram_size;
static uint8_t psram_kgd;
static uint8_t psram_eid;

/* Detected PSRAM size in bytes (0 if absent/unrecognised). */
size_t psram_get_size(void)
{
	return psram_size;
}

/*
 * Probe the APS6404 ID over QMI direct mode and decode its density.
 * Runs from SRAM: it enables QMI direct mode, which makes flash XIP (CS0)
 * unavailable for its duration.
 */
static size_t __ramfunc psram_detect(void)
{
	size_t size = 0;
	uint8_t kgd = 0;
	uint8_t eid = 0;

	/* Try to read the PSRAM ID via direct_csr. */
	qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;

	/* Wait for any cooldown on the last XIP transfer to expire before the
	 * first direct-mode operation.
	 */
	while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
	}

	/* Exit quad mode in case we (or the bootrom) already inited it. */
	qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
	qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS |
			    QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB | 0xf5;
	while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
	}
	(void)qmi_hw->direct_rx;
	qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);

	/* Read the ID: cmd 0x9F, then clock out bytes; KGD is byte 5, EID byte 6. */
	qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
	for (size_t i = 0; i < 7; i++) {
		qmi_hw->direct_tx = (i == 0) ? 0x9f : 0xff;
		while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0) {
		}
		while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
		}
		if (i == 5) {
			kgd = qmi_hw->direct_rx;
		} else if (i == 6) {
			eid = qmi_hw->direct_rx;
		} else {
			(void)qmi_hw->direct_rx;
		}
	}

	qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

	psram_kgd = kgd;
	psram_eid = eid;

	if (kgd == 0x5d) { /* known-good die */
		uint8_t size_id = eid >> 5;

		size = 1024 * 1024; /* 1 MiB base */
		if (eid == 0x26 || size_id == 2) {
			size *= 8; /* 8 MiB */
		} else if (size_id == 0) {
			size *= 2; /* 2 MiB */
		} else if (size_id == 1) {
			size *= 4; /* 4 MiB */
		}
	}

	return size;
}

/*
 * Bring up the PSRAM and map it into XIP window 1. Returns the detected size
 * in bytes, 0 if no (recognised) device is present.
 */
static size_t __ramfunc psram_setup(unsigned int cs_pin)
{
	/* Route the CS pad to the QMI XIP CS1 function (flash XIP still up here). */
	gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

	uint32_t intr_stash = save_and_disable_interrupts();

	size_t size = psram_detect();

	if (size == 0) {
		restore_interrupts(intr_stash);
		return 0;
	}

	/*
	 * Compute everything BEFORE entering direct mode — flash XIP (CS0) is
	 * unavailable while direct mode is on, so no flash-resident code may run
	 * then. In particular the 64-bit division below compiles to an
	 * __aeabi_ldivmod call that lives in flash; doing it inside direct mode
	 * faults the core. (pico-sdk keeps divide helpers in RAM; Zephyr does not.)
	 *
	 * APS6404 timing (per MicroPython rp2_psram.c): SCK <= 133 MHz; add
	 * rxdelay margin above 100 MHz; bound CS-assert to <= 8 us (tCEM) and
	 * CS-deselect to >= 18 ns (tCPH).
	 */
	const int clock_hz = PSRAM_SYS_CLK_HZ;
	int divisor = (clock_hz + APS6404_MAX_FREQ_HZ - 1) / APS6404_MAX_FREQ_HZ;

	if (divisor == 1 && clock_hz > 100000000) {
		divisor = 2;
	}
	int rxdelay = divisor;

	if (clock_hz / divisor > 100000000) {
		rxdelay += 1;
	}

	const int clock_period_fs = 1000000000000000ll / clock_hz;
	const int max_select = (125 * 1000000) / clock_period_fs;      /* 8000ns / 64 */
	const int min_deselect =
		(18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs - (divisor + 1) / 2;

	/* Enter direct mode, PSRAM CS, slow clkdiv for the config commands. */
	qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS |
			     QMI_DIRECT_CSR_AUTO_CS1N_BITS;
	while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
	}

	/* Enable QPI mode on the PSRAM (0x35). */
	qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | 0x35;
	while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
	}

	qmi_hw->m[1].timing = 1 << QMI_M1_TIMING_COOLDOWN_LSB |
			      QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
			      max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
			      min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
			      rxdelay << QMI_M1_TIMING_RXDELAY_LSB |
			      divisor << QMI_M1_TIMING_CLKDIV_LSB;

	/* Quad read (0xEB) and quad write (0x38) formats. */
	qmi_hw->m[1].rfmt = QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB |
			    QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB |
			    QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |
			    QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB |
			    QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB |
			    QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB |
			    6 << QMI_M0_RFMT_DUMMY_LEN_LSB;
	qmi_hw->m[1].rcmd = 0xeb;

	qmi_hw->m[1].wfmt = QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |
			    QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB |
			    QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |
			    QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB |
			    QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB |
			    QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB;
	qmi_hw->m[1].wcmd = 0x38;

	/* Leave direct mode and allow XIP writes through window 1. */
	qmi_hw->direct_csr = 0;
	hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);

	restore_interrupts(intr_stash);

	return size;
}

static int psram_init(void)
{
	psram_size = psram_setup(PSRAM_CS_PIN);

	LOG_DBG("APS6404 ID: KGD=0x%02x EID=0x%02x", psram_kgd, psram_eid);

	if (psram_size == 0) {
		LOG_ERR("PSRAM not detected on GP%d (QMI CS1): KGD=0x%02x (expected 0x5d)",
			PSRAM_CS_PIN, psram_kgd);
		return 0; /* non-fatal: leave the region unused */
	}

	if (psram_size != PSRAM_DT_SIZE) {
		LOG_WRN("PSRAM detected %zu MiB but devicetree declares %u MiB",
			psram_size >> 20, (unsigned int)(PSRAM_DT_SIZE >> 20));
	}

	LOG_INF("PSRAM %zu MiB ready at 0x%08lx", psram_size >> 20, (unsigned long)PSRAM_BASE);

	return 0;
}

/*
 * POST_KERNEL, not PRE_KERNEL: the QMI bring-up needs clk_sys already at its
 * final speed and the GPIO/QMI peripherals out of reset (mirrors MicroPython,
 * which calls psram_init() from main() after set_sys_clock_khz()). Runs before
 * the application main(), so CONFIG_MEM_ATTR_HEAP can allocate from it.
 */
SYS_INIT(psram_init, POST_KERNEL, CONFIG_PIMORONI_RP2350_PSRAM_INIT_PRIORITY);
