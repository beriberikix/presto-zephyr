/*
 * microSD bring-up test for the Pimoroni Presto.
 *
 * The slot is wired for 4-bit SDIO but driven here in SPI mode on the hardware
 * SPI0 controller (SCLK=GP34, MOSI=GP35, MISO=GP36, CS=GP39 as a plain GPIO).
 * See boards/pimoroni/presto/presto.dtsi for why SPI and not SDIO.
 *
 * Reports the card's geometry, mounts FAT, lists the root directory, and
 * round-trips a file so a pass means the card is genuinely writable and not
 * merely detected.
 *
 * Copyright (c) 2026 Jonathan Beri
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_DISK_DRIVER_SDMMC)
#include <zephyr/drivers/spi.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/fs/fs.h>
#include <ff.h>
#endif

LOG_MODULE_REGISTER(test_sdcard, LOG_LEVEL_INF);

#if defined(CONFIG_DISK_DRIVER_SDMMC)

/* Matches disk-name in the sdmmc node, which is what makes the mount point
 * "/SD:" -- FATFS derives the volume name from the disk, so the two cannot be
 * chosen independently.
 */
#define DISK_NAME  "SD"
#define MOUNT_ROOT "/" DISK_NAME ":"

#define TEST_FILE MOUNT_ROOT "/presto.txt"
#define TEST_TEXT "written by presto-zephyr test_sdcard\n"

static FATFS fat_fs;

static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = MOUNT_ROOT,
};

/* Clock ten idle bytes out of SPI0 with no chip select asserted, and report what
 * comes back on MISO. This runs before the SD stack touches the bus and tells
 * the three failure modes apart, which the stack's own "CMD0 timeout" cannot:
 *
 *   0xFF repeated -- the line idles high. The bus is sane and the card is
 *                    simply not answering (or is not seated).
 *   0x00 repeated -- MISO is stuck low: wrong pin, no pull-up, or the card is
 *                    holding the line.
 *   anything else -- the card is talking, so the wiring is fine and the fault
 *                    is in the protocol layer above.
 */
static void probe_bus(void)
{
	const struct device *spi = DEVICE_DT_GET(DT_NODELABEL(spi0));
	uint8_t tx[10], rx[10] = {0};
	struct spi_config cfg = {
		.frequency = 400000,
		.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
	};
	const struct spi_buf txb = {.buf = tx, .len = sizeof(tx)};
	const struct spi_buf rxb = {.buf = rx, .len = sizeof(rx)};
	const struct spi_buf_set txs = {.buffers = &txb, .count = 1};
	const struct spi_buf_set rxs = {.buffers = &rxb, .count = 1};
	int ret;

	if (!device_is_ready(spi)) {
		LOG_ERR("spi0 not ready -- the controller never initialised");
		return;
	}

	memset(tx, 0xff, sizeof(tx));

	ret = spi_transceive(spi, &cfg, &txs, &rxs);
	if (ret != 0) {
		LOG_ERR("raw spi_transceive failed (%d)", ret);
		return;
	}

	LOG_INF("bus probe (CS idle): %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7], rx[8], rx[9]);
}

/* Card geometry, straight off the block device. This runs before any mount, so
 * it separates "the card does not answer" from "the card answers but holds no
 * filesystem" -- two failures that look identical from a failed mount alone.
 */
static int report_geometry(void)
{
	uint32_t block_count = 0, block_size = 0;
	int ret;

	ret = disk_access_init(DISK_NAME);
	if (ret != 0) {
		LOG_ERR("disk_access_init(%s) failed (%d) -- no card, or it did "
			"not answer CMD0/CMD8 over SPI",
			DISK_NAME, ret);
		return ret;
	}

	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &block_count);
	if (ret != 0) {
		LOG_ERR("sector count ioctl failed (%d)", ret);
		return ret;
	}

	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &block_size);
	if (ret != 0) {
		LOG_ERR("sector size ioctl failed (%d)", ret);
		return ret;
	}

	/* 64-bit so a card above 4 GB does not silently wrap. */
	LOG_INF("card: %u sectors x %u bytes = %llu MiB", block_count, block_size,
		((uint64_t)block_count * block_size) >> 20);

	return 0;
}

static int list_root(void)
{
	struct fs_dir_t dir;
	int ret, entries = 0;

	fs_dir_t_init(&dir);

	ret = fs_opendir(&dir, MOUNT_ROOT);
	if (ret != 0) {
		LOG_ERR("opendir(%s) failed (%d)", MOUNT_ROOT, ret);
		return ret;
	}

	LOG_INF("contents of %s:", MOUNT_ROOT);

	while (true) {
		struct fs_dirent ent;

		ret = fs_readdir(&dir, &ent);
		if (ret != 0) {
			LOG_ERR("readdir failed (%d)", ret);
			break;
		}
		if (ent.name[0] == '\0') {
			break; /* end of directory */
		}

		if (ent.type == FS_DIR_ENTRY_DIR) {
			LOG_INF("  [DIR ] %s", ent.name);
		} else {
			LOG_INF("  [FILE] %s (%zu bytes)", ent.name, ent.size);
		}
		entries++;
	}

	fs_closedir(&dir);

	if (entries == 0) {
		LOG_INF("  (empty)");
	}

	return ret;
}

/* Write a file, close it, read it back and compare. Detection and mounting both
 * pass on a card that is write-protected or whose MISO is mis-wired, so the
 * round trip is the check that actually proves the bus works both ways.
 */
static int roundtrip_file(void)
{
	struct fs_file_t file;
	char buf[sizeof(TEST_TEXT)] = {0};
	struct fs_dirent ent;
	int ret, closed;

	fs_file_t_init(&file);

	ret = fs_open(&file, TEST_FILE, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		LOG_ERR("open(%s) for write failed (%d)", TEST_FILE, ret);
		return ret;
	}

	ret = fs_write(&file, TEST_TEXT, strlen(TEST_TEXT));

	/* Check the close, not just the write.
	 *
	 * FATFS does not commit the directory entry when the data is written --
	 * it commits it in f_close. So a discarded close return turns "the card
	 * accepted every block and then failed to publish the file" into a plain
	 * ENOENT on the next open, blamed on the wrong call entirely.
	 */
	closed = fs_close(&file);

	if (ret < 0) {
		LOG_ERR("write failed (%d)", ret);
		return ret;
	}
	if (ret != (int)strlen(TEST_TEXT)) {
		LOG_ERR("short write: %d of %zu bytes", ret, strlen(TEST_TEXT));
		return -EIO;
	}
	if (closed != 0) {
		LOG_ERR("close after write failed (%d) -- the data went to the card "
			"but the directory entry was never committed",
			closed);
		return closed;
	}

	LOG_INF("wrote %d bytes to %s and closed cleanly", ret, TEST_FILE);

	/* Ask the filesystem whether the file now exists, before trying to open
	 * it. Separates "it was never created" from "it exists but will not
	 * open".
	 */
	ret = fs_stat(TEST_FILE, &ent);
	if (ret != 0) {
		LOG_ERR("stat(%s) failed (%d) -- the file is genuinely not there",
			TEST_FILE, ret);
		return ret;
	}
	LOG_INF("stat: %s is %zu bytes", ent.name, ent.size);

	fs_file_t_init(&file);

	ret = fs_open(&file, TEST_FILE, FS_O_READ);
	if (ret != 0) {
		LOG_ERR("open(%s) for read failed (%d)", TEST_FILE, ret);
		return ret;
	}

	ret = fs_read(&file, buf, sizeof(buf) - 1);
	closed = fs_close(&file);

	if (ret < 0) {
		LOG_ERR("read failed (%d)", ret);
		return ret;
	}
	if (closed != 0) {
		LOG_ERR("close after read failed (%d)", closed);
		return closed;
	}

	if (strncmp(buf, TEST_TEXT, strlen(TEST_TEXT)) != 0) {
		LOG_ERR("read back did not match what was written");
		return -EIO;
	}

	LOG_INF("round-trip of %s OK (%d bytes)", TEST_FILE, ret);
	return 0;
}

static void run(void)
{
	int ret;

	probe_bus();

	ret = report_geometry();
	if (ret != 0) {
		return;
	}

	ret = fs_mount(&mp);
	if (ret != 0) {
		LOG_ERR("mount %s failed (%d) -- the card answered, so it most "
			"likely holds no FAT filesystem",
			MOUNT_ROOT, ret);
		return;
	}

	LOG_INF("mounted %s", MOUNT_ROOT);

	if (list_root() != 0) {
		goto out;
	}
	if (roundtrip_file() != 0) {
		goto out;
	}

	LOG_INF("microSD: all checks passed");
out:
	fs_unmount(&mp);
}

#else /* !CONFIG_DISK_DRIVER_SDMMC */

static void run(void)
{
	LOG_INF("microSD not enabled on this board -- skipping");
}

#endif

/*
 * A USB CDC console does not exist until a host opens the port, and anything
 * printed before it does is discarded rather than buffered. A one-shot test
 * that prints at boot therefore reports *nothing* over `-S cdc-acm-console`,
 * which on a board with no other console reads exactly like a hung image.
 *
 * Waiting for DTR once, with a timeout, is not enough either: the window
 * between the board rebooting out of the UF2 bootloader and a terminal being
 * attached is however long the human takes, and when the timeout loses that
 * race the output is gone with no way back but another reset.
 *
 * So run the test on every DTR assertion instead. Attach a terminal whenever
 * -- at boot, or ten minutes later -- and the card is tested afresh; detach and
 * reattach to run it again. Nothing to race and no reset needed.
 */
#if defined(CONFIG_UART_LINE_CTRL)

#include <zephyr/drivers/uart.h>

/* True once the line state matches `want`, false if this console has no line
 * control at all (a plain UART), which is the caller's cue to stop asking.
 */
static bool wait_for_dtr(const struct device *con, bool want)
{
	while (true) {
		uint32_t dtr = 0;

		if (uart_line_ctrl_get(con, UART_LINE_CTRL_DTR, &dtr) != 0) {
			return false;
		}
		if (!!dtr == want) {
			return true;
		}
		k_sleep(K_MSEC(100));
	}
}

static void serve_console(void)
{
	const struct device *con = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (!device_is_ready(con) || !wait_for_dtr(con, true)) {
		/* No line control: an ordinary UART, always listening. */
		LOG_INF("Presto microSD test");
		run();
		return;
	}

	while (true) {
		/* The host has the port open, but its tty layer may still be
		 * settling; without this the first line or two go missing.
		 */
		k_sleep(K_MSEC(200));

		LOG_INF("Presto microSD test");
		run();

		(void)wait_for_dtr(con, false);
		(void)wait_for_dtr(con, true);
	}
}

#else

static void serve_console(void)
{
	LOG_INF("Presto microSD test");
	run();
}

#endif

int main(void)
{
	serve_console();

	return 0;
}
