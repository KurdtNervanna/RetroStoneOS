/*
 * rsos-axpstamp - the AXP209's 12 data buffer registers (REG04-REG0F), which
 * the PMIC keeps as long as it has a power source (the battery), also while
 * the system is off:
 *
 *   REG04-0C  the times of the last shutdown steps (save / show)
 *   REG0D     the memory speed setting and its trial (dram-*), read by
 *             U-Boot's SPL before it starts the DRAM (patches/uboot/0004)
 *   REG0E-0F  the eMMC boot breadcrumbs of U-Boot (patches/uboot/0003)
 *
 * Shutdown times: rcK writes /data/rsos/logs/shutdown.txt before it unmounts
 * /data, so the last steps (sync, unmount, poweroff -f) cannot be logged
 * there. rcK saves their uptimes here just before "poweroff -f", and the boot
 * logger appends them to shutdown.txt on the next boot (and clears them).
 *
 *   rsos-axpstamp save <t_log> <t_sync> <t_umount>
 *       uptimes in seconds ("12.34", as in /proc/uptime) of "log written",
 *       "sync done" and "/data unmounted"; the current uptime is added as
 *       the fourth time ("poweroff -f"). Stored in REG04-0C: the first as
 *       24-bit centiseconds, the three steps after it as 16-bit differences.
 *   rsos-axpstamp show
 *       prints "<t_log> <t_sync> <t_umount> <t_poweroff>" in seconds and
 *       clears REG04-0C; exit status 1 (nothing printed) if they hold no
 *       plausible record.
 *   rsos-axpstamp raw
 *       REG04-0F in hex, unchanged (rcS, before "show" clears them).
 *
 * Memory speed (Settings > Power > Memory speed, the RetroStone2): REG0D =
 * <state> << 4 | <level>, level 1 = 432 MHz, 2 = 480 MHz (0 or anything else:
 * the normal 384 MHz). States: A requested, B trial running (set by the SPL
 * when it uses the level), C confirmed (set by rsos-boot-ok after a stable
 * start and a memory test; the SPL turns it into B again at every start, so
 * any start that is not confirmed falls back), F failed (the SPL found B:
 * that start never got confirmed, the normal speed is used).
 *   rsos-axpstamp dram-get     "<normal|requested|trial|on|failed> <level> <MHz>"
 *   rsos-axpstamp dram-set N   0 = normal; 1, 2 = try that level at the next start
 *   rsos-axpstamp dram-ok      B -> C (rsos-boot-ok)
 *   rsos-axpstamp dram-fail    B -> F (the memory test failed)
 *   rsos-axpstamp dram-retry   B -> A (an orderly shutdown before the
 *                              confirmation: try again next time)
 *   -b <N>  I2C bus number (default 0: the A20's TWI0, where the AXP209 is)
 *
 * The AXP209 (0x34) is bound to the axp20x driver, so plain I2C_SLAVE
 * would fail with EBUSY; I2C_RDWR transfers go through the adapter's lock
 * next to the driver's own (as rsos-bootreason does). One register per
 * transfer: the AXP209 does not auto-increment on writes.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define AXP_ADDR	0x34
#define AXP_DATA0	0x04
#define NVALS		4
#define STAMP_BYTES	9		/* REG04-0C */
#define REG_DRAM	0x0d
#define MAX_CS		0xffffff	/* 24 bits of centiseconds: 46 hours */

static int reg_write(int fd, uint8_t reg, uint8_t val)
{
	uint8_t buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = AXP_ADDR, .flags = 0, .len = 2, .buf = buf };
	struct i2c_rdwr_ioctl_data x = { .msgs = &msg, .nmsgs = 1 };

	return ioctl(fd, I2C_RDWR, &x) == 1 ? 0 : -1;
}

static int reg_read(int fd, uint8_t reg, uint8_t *val)
{
	struct i2c_msg msgs[2] = {
		{ .addr = AXP_ADDR, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = AXP_ADDR, .flags = I2C_M_RD, .len = 1, .buf = val },
	};
	struct i2c_rdwr_ioctl_data x = { .msgs = msgs, .nmsgs = 2 };

	return ioctl(fd, I2C_RDWR, &x) == 2 ? 0 : -1;
}

/* "12.34" -> 1234 (centiseconds); -1 if not a number */
static long parse_cs(const char *s)
{
	char *end;
	double v = strtod(s, &end);

	if (end == s || v < 0 || v * 100 > MAX_CS)
		return -1;
	return (long)(v * 100 + 0.5);
}

static long uptime_cs(void)
{
	char buf[64];
	FILE *f = fopen("/proc/uptime", "r");
	long cs = -1;

	if (f) {
		if (fgets(buf, sizeof(buf), f))
			cs = parse_cs(buf);
		fclose(f);
	}
	return cs;
}

static int usage(void)
{
	fprintf(stderr, "usage: rsos-axpstamp [-b N] save <t_log> <t_sync> <t_umount>\n"
			"       rsos-axpstamp [-b N] show | raw\n"
			"       rsos-axpstamp [-b N] dram-get | dram-set <0|1|2> | dram-ok | dram-fail | dram-retry\n");
	return 2;
}

static int open_bus(const char *dev)
{
	int fd = open(dev, O_RDWR | O_CLOEXEC);

	if (fd < 0)
		fprintf(stderr, "rsos-axpstamp: %s: %s\n", dev, strerror(errno));
	return fd;
}

/* raw: REG04-0F in hex, unchanged */
static int show_raw(const char *dev)
{
	uint8_t v;
	int fd = open_bus(dev), j;

	if (fd < 0)
		return 1;
	for (j = 0; j < 12; j++) {
		if (reg_read(fd, AXP_DATA0 + j, &v) < 0) {
			fprintf(stderr, "rsos-axpstamp: read REG%02X: %s\n", AXP_DATA0 + j, strerror(errno));
			close(fd);
			return 1;
		}
		printf("%s%02x", j ? " " : "", v);
	}
	printf("\n");
	close(fd);
	return 0;
}

static int dram_cmd(const char *dev, const char *cmd, const char *arg)
{
	static const char *const states[16] = {
		[0xa] = "requested", [0xb] = "trial", [0xc] = "on", [0xf] = "failed",
	};
	static const int mhz[3] = { 384, 432, 480 };
	uint8_t v, st, lvl;
	int fd = open_bus(dev), r = 0;

	if (fd < 0)
		return 1;
	if (reg_read(fd, REG_DRAM, &v) < 0) {
		fprintf(stderr, "rsos-axpstamp: read REG%02X: %s\n", REG_DRAM, strerror(errno));
		close(fd);
		return 1;
	}
	st = v >> 4;
	lvl = v & 0x0f;
	if (lvl < 1 || lvl > 2 || !states[st]) {
		st = 0;
		lvl = 0;
	}
	if (!strcmp(cmd, "dram-get")) {
		printf("%s %d %d\n", st ? states[st] : "normal", lvl, mhz[st == 0xa || st == 0xf ? 0 : lvl]);
	} else if (!strcmp(cmd, "dram-set") && arg) {
		int n = atoi(arg);

		if (n < 0 || n > 2 || arg[0] < '0' || arg[0] > '2' || arg[1])
			r = 2;
		else
			r = reg_write(fd, REG_DRAM, n ? (uint8_t)(0xa0 | n) : 0) < 0;
	} else if (!strcmp(cmd, "dram-ok") || !strcmp(cmd, "dram-fail") || !strcmp(cmd, "dram-retry")) {
		uint8_t to = cmd[5] == 'o' ? 0xc0 : cmd[5] == 'f' ? 0xf0 : 0xa0;

		if (st == 0xb)
			r = reg_write(fd, REG_DRAM, (uint8_t)(to | lvl)) < 0;
	} else {
		r = usage();
	}
	close(fd);
	return r;
}

int main(int argc, char **argv)
{
	char dev[32];
	int bus = 0, fd, i, j;
	long cs[NVALS];
	uint8_t raw[STAMP_BYTES];

	if (argc > 2 && strcmp(argv[1], "-b") == 0) {
		bus = atoi(argv[2]);
		argv += 2;
		argc -= 2;
	}
	if (argc < 2)
		return usage();
	snprintf(dev, sizeof(dev), "/dev/i2c-%d", bus);

	if (!strncmp(argv[1], "dram-", 5))
		return dram_cmd(dev, argv[1], argc > 2 ? argv[2] : NULL);

	if (strcmp(argv[1], "save") == 0) {
		if (argc != 5)
			return usage();
		for (i = 0; i < 3; i++)
			cs[i] = parse_cs(argv[2 + i]);
		cs[3] = uptime_cs();
		for (i = 0; i < NVALS; i++)
			if (cs[i] < 0)
				return usage();
		raw[0] = cs[0] >> 16;
		raw[1] = cs[0] >> 8;
		raw[2] = cs[0];
		for (i = 1; i < NVALS; i++) {
			long d = cs[i] - cs[i - 1];

			if (d < 0)
				d = 0;
			if (d > 0xffff)
				d = 0xffff;
			raw[1 + 2 * i] = d >> 8;
			raw[2 + 2 * i] = d;
		}
		fd = open_bus(dev);
		if (fd < 0)
			return 1;
		for (j = 0; j < STAMP_BYTES; j++) {
			if (reg_write(fd, AXP_DATA0 + j, raw[j]) < 0) {
				fprintf(stderr, "rsos-axpstamp: write REG%02X: %s\n",
					AXP_DATA0 + j, strerror(errno));
				close(fd);
				return 1;
			}
		}
		close(fd);
		return 0;
	}

	if (strcmp(argv[1], "raw") == 0 && argc == 2)
		return show_raw(dev);

	if (strcmp(argv[1], "show") == 0 && argc == 2) {
		int ok = 1;

		fd = open_bus(dev);
		if (fd < 0)
			return 1;
		for (j = 0; j < STAMP_BYTES; j++) {
			if (reg_read(fd, AXP_DATA0 + j, &raw[j]) < 0) {
				fprintf(stderr, "rsos-axpstamp: read REG%02X: %s\n",
					AXP_DATA0 + j, strerror(errno));
				close(fd);
				return 1;
			}
		}
		cs[0] = (long)raw[0] << 16 | raw[1] << 8 | raw[2];
		for (i = 1; i < NVALS; i++)
			cs[i] = cs[i - 1] + (raw[1 + 2 * i] << 8 | raw[2 + 2 * i]);
		/* Plausible: non-zero, all within a minute */
		if (cs[0] == 0 || cs[NVALS - 1] - cs[0] > 6000)
			ok = 0;
		/* Report once: clear the registers (never REG0D-0F) */
		for (j = 0; j < STAMP_BYTES; j++)
			if (raw[j])
				reg_write(fd, AXP_DATA0 + j, 0);
		close(fd);
		if (!ok)
			return 1;
		for (i = 0; i < NVALS; i++)
			printf("%s%ld.%02ld", i ? " " : "", cs[i] / 100, cs[i] % 100);
		printf("\n");
		return 0;
	}

	return usage();
}
