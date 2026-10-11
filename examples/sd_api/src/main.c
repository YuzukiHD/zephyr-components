/*
 * SD card protocol stack on the TF slot of the YuzukiNeko (SMHC0). Every interface of the three
 * layers is called once:
 *   - the SD subsystem: <zephyr/sd/sd.h> and <zephyr/sd/sdmmc.h>
 *   - the disk access layer: <zephyr/storage/disk_access.h>
 *   - the FAT file system through <zephyr/fs/fs.h>
 * Nothing on the card is changed in the end: sectors are written back with the data they had, and
 * the files and the directory that are created are deleted again.
 */
#include <errno.h>
#include <ff.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sd/sd.h>
#include <zephyr/sd/sdmmc.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>

/* the host controller, and the disk named in the sdmmc node of the board (disk-name = "SD") */
static const struct device *const sdhc = DEVICE_DT_GET(DT_NODELABEL(smhc0));
#define DISK_NAME	"SD"

#define MOUNT_POINT	"/SD:"
#define TEST_FILE	MOUNT_POINT "/F101API.TXT"
#define TEST_FILE2	MOUNT_POINT "/F101NEW.TXT"
#define TEST_DIR	MOUNT_POINT "/F101DIR"

static const char *err(int ret)
{
	switch (ret) {
	case 0:		return "0";
	case -EIO:	return "-EIO";
	case -EINVAL:	return "-EINVAL";
	case -ENOENT:	return "-ENOENT";
	case -ENODEV:	return "-ENODEV";
	case -ENOTSUP:	return "-ENOTSUP";
	case -EBUSY:	return "-EBUSY";
	case -ETIMEDOUT: return "-ETIMEDOUT";
	case -EEXIST:	return "-EEXIST";
	case -ENOTDIR:	return "-ENOTDIR";
	case -EROFS:	return "-EROFS";
	case -EACCES:	return "-EACCES";
	default:	return "?";
	}
}

/* a 1 MiB buffer aligned to the cache line, and one that is used on purpose unaligned */
static uint8_t big[1024 * 1024 + 64] __aligned(64);

/* ---------------------------------------------------------------- SD subsystem */

/* the structure of the subsystem: one per card. sd_init fills it in. */
static struct sd_card card;

static void demo_sd_is_card_present(void)
{
	printk("[sd_is_card_present] controller = %d (1 = a card is in the slot)\n",
	       sd_is_card_present(sdhc));
	printk("[sd_is_card_present] NULL controller = %d\n", sd_is_card_present(NULL));
}

static void demo_sd_init(void)
{
	int64_t t0 = k_uptime_get();
	int ret = sd_init(sdhc, &card);

	printk("[sd_init] sd_init = %s, %lld ms\n", err(ret), k_uptime_get() - t0);
	printk("[sd_init] status %d (2 = initialized), type %d (0 = SD memory card)\n",
	       card.status, card.type);
	printk("[sd_init] OCR %08x, RCA %04x, SD spec version flag %u\n", card.ocr,
	       card.relative_addr, card.sd_version);
	printk("[sd_init] %u blocks of %u bytes, bus width %u bit, clock %u Hz, timing mode %u\n",
	       card.block_count, card.block_size, card.bus_io.bus_width, card.bus_io.clock,
	       card.bus_io.timing);
	printk("[sd_init] sd_init with a NULL controller = %s\n", err(sd_init(NULL, &card)));
}

static void demo_sdmmc_ioctl(void)
{
	uint32_t v = 0;
	int ret;

	ret = sdmmc_ioctl(&card, DISK_IOCTL_GET_SECTOR_COUNT, &v);
	printk("[sdmmc_ioctl] DISK_IOCTL_GET_SECTOR_COUNT = %s: %u\n", err(ret), v);
	ret = sdmmc_ioctl(&card, DISK_IOCTL_GET_SECTOR_SIZE, &v);
	printk("[sdmmc_ioctl] DISK_IOCTL_GET_SECTOR_SIZE = %s: %u\n", err(ret), v);
	ret = sdmmc_ioctl(&card, DISK_IOCTL_GET_ERASE_BLOCK_SZ, &v);
	printk("[sdmmc_ioctl] DISK_IOCTL_GET_ERASE_BLOCK_SZ = %s: %u\n", err(ret), v);
	ret = sdmmc_ioctl(&card, DISK_IOCTL_CTRL_SYNC, NULL);
	printk("[sdmmc_ioctl] DISK_IOCTL_CTRL_SYNC = %s\n", err(ret));
	ret = sdmmc_ioctl(&card, DISK_IOCTL_RESERVED, NULL);
	printk("[sdmmc_ioctl] DISK_IOCTL_RESERVED (not handled) = %s\n", err(ret));
}

static void demo_sdmmc_read_blocks(void)
{
	uint32_t n = 1024 * 1024 / card.block_size;
	int64_t t0;
	int ret;

	ret = sdmmc_read_blocks(&card, big, 0, 1);
	printk("[sdmmc_read_blocks] block 0 = %s, boot signature %02x %02x\n", err(ret),
	       big[510], big[511]);

	t0 = k_uptime_get();
	ret = sdmmc_read_blocks(&card, big, 0, n);
	printk("[sdmmc_read_blocks] %u blocks (1 MiB) = %s, %lld ms\n", n, err(ret),
	       k_uptime_get() - t0);

	ret = sdmmc_read_blocks(&card, big + 4, 0, 1);
	printk("[sdmmc_read_blocks] buffer + 4 bytes = %s\n", err(ret));

	ret = sdmmc_read_blocks(&card, big, card.block_count - 1, 1);
	printk("[sdmmc_read_blocks] the last block (%u) = %s\n", card.block_count - 1, err(ret));

	ret = sdmmc_read_blocks(&card, big, card.block_count, 1);
	printk("[sdmmc_read_blocks] one block beyond the end = %s\n", err(ret));
}

/* writes the last block back with the content it has: the card is not changed */
static void demo_sdmmc_write_blocks(void)
{
	static uint8_t before[512] __aligned(64), after[512] __aligned(64);
	uint32_t last = card.block_count - 1;
	int ret;

	ret = sdmmc_read_blocks(&card, before, last, 1);
	printk("[sdmmc_write_blocks] read the last block (%u) = %s\n", last, err(ret));
	ret = sdmmc_write_blocks(&card, before, last, 1);
	printk("[sdmmc_write_blocks] write it back = %s\n", err(ret));
	ret = sdmmc_ioctl(&card, DISK_IOCTL_CTRL_SYNC, NULL);
	printk("[sdmmc_write_blocks] sync = %s\n", err(ret));
	ret = sdmmc_read_blocks(&card, after, last, 1);
	printk("[sdmmc_write_blocks] read again = %s, %s\n", err(ret),
	       memcmp(before, after, sizeof(before)) == 0 ? "same data" : "DIFFERENT data");
	ret = sdmmc_write_blocks(&card, before, card.block_count, 1);
	printk("[sdmmc_write_blocks] one block beyond the end = %s\n", err(ret));
}

/* the card is powered off; the disk layer below identifies it again */
static void demo_sdmmc_deinit(void)
{
	int ret = sdmmc_ioctl(&card, DISK_IOCTL_CTRL_DEINIT, NULL);

	printk("[sdmmc_ioctl] DISK_IOCTL_CTRL_DEINIT = %s\n", err(ret));
}

/* ---------------------------------------------------------------- disk access */

static uint32_t sector_count, sector_size;

static void demo_disk_access_init(void)
{
	int64_t t0 = k_uptime_get();
	int ret = disk_access_init(DISK_NAME);

	printk("[disk_access_init] \"%s\" = %s, %lld ms\n", DISK_NAME, err(ret), k_uptime_get() - t0);
	ret = disk_access_init("NODISK");
	printk("[disk_access_init] \"NODISK\" = %s\n", err(ret));
}

static void demo_disk_access_status(void)
{
	printk("[disk_access_status] \"%s\" = %d (0 = DISK_STATUS_OK)\n", DISK_NAME,
	       disk_access_status(DISK_NAME));
	printk("[disk_access_status] \"NODISK\" = %d\n", disk_access_status("NODISK"));
}

static void demo_disk_access_ioctl(void)
{
	uint32_t erase = 0;
	int ret;

	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	printk("[disk_access_ioctl] DISK_IOCTL_GET_SECTOR_COUNT = %s: %u\n", err(ret),
	       sector_count);
	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	printk("[disk_access_ioctl] DISK_IOCTL_GET_SECTOR_SIZE = %s: %u\n", err(ret), sector_size);
	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_ERASE_BLOCK_SZ, &erase);
	printk("[disk_access_ioctl] DISK_IOCTL_GET_ERASE_BLOCK_SZ = %s: %u sectors\n", err(ret),
	       erase);
	printk("[disk_access_ioctl] capacity %u MiB\n",
	       (uint32_t)(((uint64_t)sector_count * sector_size) >> 20));
	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_CTRL_SYNC, NULL);
	printk("[disk_access_ioctl] DISK_IOCTL_CTRL_SYNC = %s\n", err(ret));
}

static void demo_disk_access_read(void)
{
	uint32_t n = 1024 * 1024 / sector_size;
	int64_t t0;
	int ret;

	ret = disk_access_read(DISK_NAME, big, 0, 1);
	printk("[disk_access_read] sector 0 = %s, boot signature %02x %02x\n", err(ret),
	       big[510], big[511]);

	t0 = k_uptime_get();
	ret = disk_access_read(DISK_NAME, big, 0, n);
	printk("[disk_access_read] %u sectors (1 MiB), aligned buffer   = %s, %lld ms\n", n,
	       err(ret), k_uptime_get() - t0);

	t0 = k_uptime_get();
	ret = disk_access_read(DISK_NAME, big + 4, 0, n);
	printk("[disk_access_read] %u sectors (1 MiB), buffer + 4 bytes = %s, %lld ms\n", n,
	       err(ret), k_uptime_get() - t0);

	ret = disk_access_read(DISK_NAME, big, sector_count - 1, 1);
	printk("[disk_access_read] the last sector (%u) = %s\n", sector_count - 1, err(ret));

	ret = disk_access_read(DISK_NAME, big, sector_count, 1);
	printk("[disk_access_read] one sector beyond the end = %s\n", err(ret));
}

/* writes the last sector back with the data it has: the card is not changed */
static void demo_disk_access_write(void)
{
	static uint8_t before[512] __aligned(64), after[512] __aligned(64);
	uint32_t last = sector_count - 1;
	int ret;

	ret = disk_access_read(DISK_NAME, before, last, 1);
	printk("[disk_access_write] read the last sector (%u) = %s\n", last, err(ret));
	ret = disk_access_write(DISK_NAME, before, last, 1);
	printk("[disk_access_write] write it back = %s\n", err(ret));
	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_CTRL_SYNC, NULL);
	printk("[disk_access_write] sync = %s\n", err(ret));
	ret = disk_access_read(DISK_NAME, after, last, 1);
	printk("[disk_access_write] read again = %s, %s\n", err(ret),
	       memcmp(before, after, sizeof(before)) == 0 ? "same data" : "DIFFERENT data");
	ret = disk_access_write(DISK_NAME, before, sector_count, 1);
	printk("[disk_access_write] one sector beyond the end = %s\n", err(ret));
}

/* a deinit powers the card off; the next init identifies it again */
static void demo_disk_access_deinit(void)
{
	int ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_CTRL_DEINIT, NULL);

	printk("[disk_access_ioctl] DISK_IOCTL_CTRL_DEINIT = %s\n", err(ret));
	printk("[disk_access_status] after the deinit = %d (1 = DISK_STATUS_UNINIT)\n",
	       disk_access_status(DISK_NAME));
	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_CTRL_INIT, NULL);
	printk("[disk_access_ioctl] DISK_IOCTL_CTRL_INIT = %s\n", err(ret));
	ret = disk_access_read(DISK_NAME, big, 0, 1);
	printk("[disk_access_read] sector 0 after the init = %s, %02x %02x\n", err(ret),
	       big[510], big[511]);
}

/* ---------------------------------------------------------------- file system */

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = MOUNT_POINT,
	/* never format a card that does not mount */
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static void demo_fs_mount(void)
{
	int ret = fs_mount(&mp);

	printk("[fs_mount] %s = %s\n", MOUNT_POINT, err(ret));
	ret = fs_mount(&mp);
	printk("[fs_mount] the same mount point again = %s\n", err(ret));
}

static void demo_fs_readmount(void)
{
	const char *name;
	int index = 0;

	while (fs_readmount(&index, &name) == 0) {
		printk("[fs_readmount] mount point %d: %s\n", index - 1, name);
	}
}

static void demo_fs_statvfs(void)
{
	struct fs_statvfs sv;
	int ret = fs_statvfs(MOUNT_POINT, &sv);

	printk("[fs_statvfs] %s = %s: block size %lu, fragment size %lu, %lu blocks, %lu free\n",
	       MOUNT_POINT, err(ret), sv.f_bsize, sv.f_frsize, sv.f_blocks, sv.f_bfree);
	printk("[fs_statvfs] %u MiB total, %u MiB free\n",
	       (uint32_t)(((uint64_t)sv.f_blocks * sv.f_frsize) >> 20),
	       (uint32_t)(((uint64_t)sv.f_bfree * sv.f_frsize) >> 20));
}

static void demo_fs_dir_t_init(void)
{
	struct fs_dir_t dir;

	memset(&dir, 0xff, sizeof(dir));
	fs_dir_t_init(&dir);
	printk("[fs_dir_t_init] mp = %p, dirp = %p\n", dir.mp, dir.dirp);
}

static void demo_fs_file_t_init(void)
{
	struct fs_file_t f;

	memset(&f, 0xff, sizeof(f));
	fs_file_t_init(&f);
	printk("[fs_file_t_init] mp = %p, filep = %p, flags = %u\n", f.mp, f.filep, f.flags);
}

static void demo_fs_opendir(void)
{
	struct fs_dir_t dir;
	int ret;

	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, MOUNT_POINT);
	printk("[fs_opendir] %s = %s\n", MOUNT_POINT, err(ret));
	if (ret == 0) {
		fs_closedir(&dir);
	}

	ret = fs_opendir(&dir, MOUNT_POINT "/NODIR");
	printk("[fs_opendir] a directory that is not there = %s\n", err(ret));
}

/* the first entries of the root directory; an empty name ends the listing */
static void demo_fs_readdir(void)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	int ret, n = 0;

	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, MOUNT_POINT);
	if (ret != 0) {
		printk("[fs_readdir] fs_opendir = %s\n", err(ret));
		return;
	}
	while (n < 6) {
		ret = fs_readdir(&dir, &ent);
		if (ret != 0 || ent.name[0] == '\0') {
			printk("[fs_readdir] end of the directory = %s\n", err(ret));
			break;
		}
		printk("[fs_readdir]   %-4s %10u  %s\n",
		       ent.type == FS_DIR_ENTRY_DIR ? "dir" : "file", (uint32_t)ent.size, ent.name);
		n++;
	}
	fs_closedir(&dir);
}

static void demo_fs_closedir(void)
{
	struct fs_dir_t dir;
	int ret;

	fs_dir_t_init(&dir);
	ret = fs_opendir(&dir, MOUNT_POINT);
	if (ret != 0) {
		printk("[fs_closedir] fs_opendir = %s\n", err(ret));
		return;
	}
	ret = fs_closedir(&dir);
	printk("[fs_closedir] = %s\n", err(ret));
}

static void demo_fs_mkdir(void)
{
	struct fs_dirent st;
	int ret;

	ret = fs_mkdir(TEST_DIR);
	printk("[fs_mkdir] %s = %s\n", TEST_DIR, err(ret));
	ret = fs_stat(TEST_DIR, &st);
	printk("[fs_mkdir] fs_stat = %s: %s, type %s\n", err(ret), st.name,
	       st.type == FS_DIR_ENTRY_DIR ? "dir" : "file");
	ret = fs_mkdir(TEST_DIR);
	printk("[fs_mkdir] the same directory again = %s\n", err(ret));
}

static const char text[] = "Written by sd_api on the YuzukiNeko\n";

static void demo_fs_open(void)
{
	struct fs_file_t f;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, TEST_FILE, FS_O_CREATE | FS_O_RDWR);
	printk("[fs_open] %s, FS_O_CREATE | FS_O_RDWR = %s\n", TEST_FILE, err(ret));
	if (ret == 0) {
		fs_close(&f);
	}

	ret = fs_open(&f, MOUNT_POINT "/NOFILE.TXT", FS_O_READ);
	printk("[fs_open] a file that is not there, FS_O_READ = %s\n", err(ret));
	ret = fs_open(&f, "/NOMOUNT:/A.TXT", FS_O_READ);
	printk("[fs_open] a path with no mount point = %s\n", err(ret));
}

static void demo_fs_write(void)
{
	struct fs_file_t f;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, TEST_FILE, FS_O_WRITE | FS_O_TRUNC);
	if (ret != 0) {
		printk("[fs_write] fs_open = %s\n", err(ret));
		return;
	}
	ret = fs_write(&f, text, sizeof(text) - 1);
	printk("[fs_write] %u bytes = %d\n", (unsigned int)sizeof(text) - 1, ret);
	ret = fs_write(&f, text, sizeof(text) - 1);
	printk("[fs_write] the same text again = %d\n", ret);
	fs_close(&f);

	fs_file_t_init(&f);
	ret = fs_open(&f, TEST_FILE, FS_O_READ);
	if (ret == 0) {
		ret = fs_write(&f, text, 1);
		printk("[fs_write] on a file opened with FS_O_READ = %s\n", err(ret));
		fs_close(&f);
	}
}

static void demo_fs_sync(void)
{
	struct fs_file_t f;
	struct fs_dirent st;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, TEST_FILE, FS_O_WRITE | FS_O_APPEND);
	if (ret != 0) {
		printk("[fs_sync] fs_open = %s\n", err(ret));
		return;
	}
	fs_write(&f, text, sizeof(text) - 1);
	ret = fs_sync(&f);
	printk("[fs_sync] = %s\n", err(ret));
	/* the size is on the card now, while the file is still open */
	fs_stat(TEST_FILE, &st);
	printk("[fs_sync] fs_stat while the file is open: %u bytes (3 * %u)\n",
	       (uint32_t)st.size, (unsigned int)sizeof(text) - 1);
	fs_close(&f);
}

static void demo_fs_seek(void)
{
	struct fs_file_t f;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, TEST_FILE, FS_O_READ);
	if (ret != 0) {
		printk("[fs_seek] fs_open = %s\n", err(ret));
		return;
	}
	ret = fs_seek(&f, 10, FS_SEEK_SET);
	printk("[fs_seek] 10 from the start = %s, now at %ld\n", err(ret), (long)fs_tell(&f));
	ret = fs_seek(&f, 5, FS_SEEK_CUR);
	printk("[fs_seek] 5 from here = %s, now at %ld\n", err(ret), (long)fs_tell(&f));
	ret = fs_seek(&f, -4, FS_SEEK_END);
	printk("[fs_seek] -4 from the end = %s, now at %ld\n", err(ret), (long)fs_tell(&f));
	fs_close(&f);
}

static void demo_fs_tell(void)
{
	struct fs_file_t f;
	char buf[8];
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, TEST_FILE, FS_O_READ);
	if (ret != 0) {
		printk("[fs_tell] fs_open = %s\n", err(ret));
		return;
	}
	printk("[fs_tell] after fs_open = %ld\n", (long)fs_tell(&f));
	fs_read(&f, buf, sizeof(buf));
	printk("[fs_tell] after reading %u bytes = %ld\n", (unsigned int)sizeof(buf),
	       (long)fs_tell(&f));
	fs_close(&f);
}

static void demo_fs_read(void)
{
	struct fs_file_t f;
	char back[sizeof(text)] = { 0 };
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, TEST_FILE, FS_O_READ);
	if (ret != 0) {
		printk("[fs_read] fs_open = %s\n", err(ret));
		return;
	}
	ret = fs_read(&f, back, sizeof(text) - 1);
	printk("[fs_read] %d bytes: %s", ret, back);
	fs_seek(&f, 0, FS_SEEK_END);
	ret = fs_read(&f, back, sizeof(back) - 1);
	printk("[fs_read] at the end of the file = %d\n", ret);
	fs_close(&f);
}

static void demo_fs_truncate(void)
{
	struct fs_file_t f;
	struct fs_dirent st;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, TEST_FILE, FS_O_RDWR);
	if (ret != 0) {
		printk("[fs_truncate] fs_open = %s\n", err(ret));
		return;
	}
	ret = fs_truncate(&f, 10);
	printk("[fs_truncate] to 10 bytes = %s, now at %ld\n", err(ret), (long)fs_tell(&f));
	fs_sync(&f);
	fs_stat(TEST_FILE, &st);
	printk("[fs_truncate] size %u bytes\n", (uint32_t)st.size);
	ret = fs_truncate(&f, 100);
	fs_sync(&f);
	fs_stat(TEST_FILE, &st);
	printk("[fs_truncate] extended to 100 bytes = %s, size %u bytes\n", err(ret),
	       (uint32_t)st.size);
	fs_close(&f);
}

static void demo_fs_close(void)
{
	struct fs_file_t f;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, TEST_FILE, FS_O_READ);
	if (ret != 0) {
		printk("[fs_close] fs_open = %s\n", err(ret));
		return;
	}
	ret = fs_close(&f);
	printk("[fs_close] = %s\n", err(ret));
	ret = fs_close(&f);
	printk("[fs_close] the same file again = %s\n", err(ret));
}

static void demo_fs_stat(void)
{
	struct fs_dirent st;
	int ret;

	ret = fs_stat(TEST_FILE, &st);
	printk("[fs_stat] %s = %s: name %s, type %s, size %u\n", TEST_FILE, err(ret), st.name,
	       st.type == FS_DIR_ENTRY_DIR ? "dir" : "file", (uint32_t)st.size);
	ret = fs_stat(MOUNT_POINT "/NOFILE.TXT", &st);
	printk("[fs_stat] a file that is not there = %s\n", err(ret));
}

static void demo_fs_rename(void)
{
	struct fs_file_t f;
	struct fs_dirent st;
	int ret;

	ret = fs_rename(TEST_FILE, TEST_FILE2);
	printk("[fs_rename] %s to %s = %s\n", TEST_FILE, TEST_FILE2, err(ret));
	ret = fs_stat(TEST_FILE, &st);
	printk("[fs_rename] old name: fs_stat = %s\n", err(ret));
	ret = fs_stat(TEST_FILE2, &st);
	printk("[fs_rename] new name: fs_stat = %s, %u bytes\n", err(ret), (uint32_t)st.size);

	/* a new 4 byte file renamed onto a name that exists: the old file there is replaced */
	fs_file_t_init(&f);
	if (fs_open(&f, TEST_FILE, FS_O_CREATE | FS_O_WRITE) == 0) {
		fs_write(&f, "abcd", 4);
		fs_close(&f);
	}
	ret = fs_rename(TEST_FILE, TEST_FILE2);
	printk("[fs_rename] onto a name that exists = %s\n", err(ret));
	ret = fs_stat(TEST_FILE2, &st);
	printk("[fs_rename] the new name now has %u bytes (fs_stat = %s)\n", (uint32_t)st.size,
	       err(ret));
	ret = fs_rename(TEST_FILE, MOUNT_POINT "/F101OLD.TXT");
	printk("[fs_rename] a file that is not there = %s\n", err(ret));
}

static void demo_fs_unlink(void)
{
	struct fs_dirent st;
	int ret;

	ret = fs_unlink(TEST_FILE2);
	printk("[fs_unlink] %s = %s\n", TEST_FILE2, err(ret));
	ret = fs_stat(TEST_FILE2, &st);
	printk("[fs_unlink] fs_stat afterwards = %s\n", err(ret));
	ret = fs_unlink(TEST_DIR);
	printk("[fs_unlink] the empty directory %s = %s\n", TEST_DIR, err(ret));
	ret = fs_unlink(TEST_FILE2);
	printk("[fs_unlink] a file that is not there = %s\n", err(ret));
}

/* a 1 MiB file: write, read back and compare, through the FAT driver */
static uint8_t pattern[64 * 1024] __aligned(64);

static void demo_fs_speed(void)
{
	struct fs_file_t f;
	int64_t t0;
	int ret, bad = 0;

	for (size_t i = 0; i < sizeof(pattern); i++) {
		pattern[i] = (uint8_t)(i * 7 + 3);
	}

	fs_file_t_init(&f);
	ret = fs_open(&f, TEST_FILE, FS_O_CREATE | FS_O_RDWR | FS_O_TRUNC);
	if (ret != 0) {
		printk("[speed] fs_open = %s\n", err(ret));
		return;
	}

	t0 = k_uptime_get();
	for (int i = 0; i < 16; i++) {
		ret = fs_write(&f, pattern, sizeof(pattern));
		if (ret != sizeof(pattern)) {
			break;
		}
	}
	fs_sync(&f);
	printk("[speed] wrote 1 MiB in 64 KiB pieces = %d, %lld ms\n", ret, k_uptime_get() - t0);

	fs_seek(&f, 0, FS_SEEK_SET);
	t0 = k_uptime_get();
	for (int i = 0; i < 16; i++) {
		memset(big, 0, sizeof(pattern));
		ret = fs_read(&f, big, sizeof(pattern));
		if (ret != sizeof(pattern) || memcmp(big, pattern, sizeof(pattern)) != 0) {
			bad++;
		}
	}
	printk("[speed] read it back, %d piece(s) differ, %lld ms\n", bad, k_uptime_get() - t0);
	fs_close(&f);
	fs_unlink(TEST_FILE);
}

static void demo_fs_unmount(void)
{
	int ret = fs_unmount(&mp);

	printk("[fs_unmount] %s = %s\n", MOUNT_POINT, err(ret));
	ret = fs_unmount(&mp);
	printk("[fs_unmount] the same mount point again = %s\n", err(ret));
}

int main(void)
{
	if (!device_is_ready(sdhc)) {
		printk("sdhc not ready\n");
		return 0;
	}

	demo_sd_is_card_present();
	if (!sd_is_card_present(sdhc)) {
		printk("no card in the slot, nothing more to do\n");
		return 0;
	}
	demo_sd_init();
	demo_sdmmc_ioctl();
	demo_sdmmc_read_blocks();
	demo_sdmmc_write_blocks();
	demo_sdmmc_deinit();

	demo_disk_access_init();
	demo_disk_access_status();
	demo_disk_access_ioctl();
	demo_disk_access_read();
	demo_disk_access_write();
	demo_disk_access_deinit();

	demo_fs_mount();
	demo_fs_readmount();
	demo_fs_statvfs();
	demo_fs_dir_t_init();
	demo_fs_file_t_init();
	demo_fs_opendir();
	demo_fs_readdir();
	demo_fs_closedir();
	demo_fs_mkdir();
	demo_fs_open();
	demo_fs_write();
	demo_fs_sync();
	demo_fs_seek();
	demo_fs_tell();
	demo_fs_read();
	demo_fs_truncate();
	demo_fs_close();
	demo_fs_stat();
	demo_fs_rename();
	demo_fs_unlink();
	demo_fs_speed();
	demo_fs_unmount();

	printk("sd_api done\n");
	return 0;
}
