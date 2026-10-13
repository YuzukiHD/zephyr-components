/* Read-ahead of the ROM pages that do not fit in memory.
 *
 * A ROM larger than the buffer is paged in 32 KiB at a time from the file; a page that is not in
 * memory is read in the middle of the frame (about 2 ms), and a scene change that needs thirty of
 * them makes a frame of 60 ms. Here the pages go through a small ring of staging buffers:
 *
 *  - on a miss the page and the ones after it are read with one larger card access (a run of up to
 *    GBSP_ROM_READAHEAD pages), the wanted one is copied to its place;
 *  - the pages after the run are then asked for from a background thread, which reads them while
 *    the emulation does something else; the next miss finds its page staged and only copies it.
 *
 * The thread never touches the tables of the emulation (which page is where): it only fills the
 * staging buffers and says so. All access to the file is serialised by file_lock.
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rom_prefetch.h"

#define PAGE_BYTES	(32 * 1024)
#define STAGE		CONFIG_GBSP_ROM_STAGE_PAGES
#define RUN		CONFIG_GBSP_ROM_READAHEAD
#define FREE_SLOT	0
#define READING		1
#define READY		2

static struct fs_file_t *file;
static uint32_t rom_pages;
static bool (*resident)(uint32_t page);
static uint8_t *stage_buf;
static int32_t stage_page[STAGE];
static uint8_t stage_state[STAGE];
static uint32_t stage_head;
static bool active;

static K_MUTEX_DEFINE(file_lock);
static K_MUTEX_DEFINE(state_lock);
static K_CONDVAR_DEFINE(stage_cv);
static K_SEM_DEFINE(kick, 0, 1);

/* what the thread is asked to fetch next, the newest request replaces the older one */
static struct {
	uint32_t page[RUN];
	uint32_t n;
} want;

static K_THREAD_STACK_DEFINE(thread_stack, 4096);
static struct k_thread thread;
static volatile bool stop;

uint32_t gbsp_rom_hits;

static int find_slot(uint32_t page)
{
	for (int i = 0; i < STAGE; i++) {
		if (stage_page[i] == (int32_t)page && stage_state[i] != FREE_SLOT) {
			return i;
		}
	}

	return -1;
}

/* reads pages first .. first + n - 1 (n <= RUN, consecutive) into the staging ring */
static void stage_fetch(uint32_t first, uint32_t n)
{
	uint32_t slot;
	ssize_t got = -1;

	k_mutex_lock(&file_lock, K_FOREVER);
	k_mutex_lock(&state_lock, K_FOREVER);
	/* what is staged already (or was read while this waited for the file) is not read again */
	while (n > 0 && find_slot(first) >= 0) {
		first++;
		n--;
	}
	while (n > 0 && find_slot(first + n - 1) >= 0) {
		n--;
	}
	if (n == 0 || stop) {
		k_mutex_unlock(&state_lock);
		k_mutex_unlock(&file_lock);
		return;
	}
	slot = stage_head;
	if (slot + n > STAGE) {
		slot = 0;
	}
	for (uint32_t i = 0; i < n; i++) {
		stage_page[slot + i] = first + i;
		stage_state[slot + i] = READING;
	}
	stage_head = (slot + n) % STAGE;
	k_mutex_unlock(&state_lock);

	if (fs_seek(file, (off_t)first * PAGE_BYTES, FS_SEEK_SET) == 0) {
		got = fs_read(file, stage_buf + (size_t)slot * PAGE_BYTES, n * PAGE_BYTES);
	}

	k_mutex_lock(&state_lock, K_FOREVER);
	for (uint32_t i = 0; i < n; i++) {
		if (got >= (ssize_t)((i + 1) * PAGE_BYTES)) {
			stage_state[slot + i] = READY;
		} else {
			stage_page[slot + i] = -1;
			stage_state[slot + i] = FREE_SLOT;
		}
	}
	k_condvar_broadcast(&stage_cv);
	k_mutex_unlock(&state_lock);
	k_mutex_unlock(&file_lock);
}

static void prefetch_main(void *a, void *b, void *c)
{
	for (;;) {
		uint32_t pages[RUN], n;

		k_sem_take(&kick, K_FOREVER);
		if (stop) {
			return;
		}
		k_mutex_lock(&state_lock, K_FOREVER);
		n = want.n;
		memcpy(pages, want.page, sizeof(pages));
		want.n = 0;
		k_mutex_unlock(&state_lock);
		/* consecutive pages go in one read */
		for (uint32_t i = 0; i < n && !stop;) {
			uint32_t j = i + 1;

			while (j < n && pages[j] == pages[j - 1] + 1) {
				j++;
			}
			stage_fetch(pages[i], j - i);
			i = j;
		}
	}
}

int rom_prefetch_start(struct fs_file_t *f, uint32_t pages, bool (*is_resident)(uint32_t page))
{
	stage_buf = aligned_alloc(64, (size_t)STAGE * PAGE_BYTES);
	if (stage_buf == NULL) {
		return -1;
	}
	file = f;
	rom_pages = pages;
	resident = is_resident;
	for (int i = 0; i < STAGE; i++) {
		stage_page[i] = -1;
		stage_state[i] = FREE_SLOT;
	}
	stage_head = 0;
	want.n = 0;
	stop = false;
	gbsp_rom_hits = 0;
	k_sem_reset(&kick);
	k_thread_create(&thread, thread_stack, K_THREAD_STACK_SIZEOF(thread_stack), prefetch_main,
			NULL, NULL, NULL, CONFIG_GBSP_ROM_PREFETCH_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&thread, "gbsp_rom");
	active = true;

	return 0;
}

void rom_prefetch_stop(void)
{
	if (!active) {
		return;
	}
	stop = true;
	k_sem_give(&kick);
	k_mutex_lock(&state_lock, K_FOREVER);
	k_condvar_broadcast(&stage_cv);
	k_mutex_unlock(&state_lock);
	k_thread_join(&thread, K_FOREVER);
	free(stage_buf);
	stage_buf = NULL;
	active = false;
}

bool rom_prefetch_active(void)
{
	return active;
}

/* copies a staged page to dst and frees its slot; waits for a page that is being read */
static bool take_staged(uint32_t page, uint8_t *dst)
{
	bool found = false;

	k_mutex_lock(&state_lock, K_FOREVER);
	for (;;) {
		int s = find_slot(page);

		if (s < 0) {
			break;
		}
		if (stage_state[s] == READY) {
			memcpy(dst, stage_buf + (size_t)s * PAGE_BYTES, PAGE_BYTES);
			stage_state[s] = FREE_SLOT;
			stage_page[s] = -1;
			found = true;
			break;
		}
		k_condvar_wait(&stage_cv, &state_lock, K_FOREVER);
	}
	k_mutex_unlock(&state_lock);

	return found;
}

/* asks the thread for the pages after `last` that are neither in memory nor staged */
static void ask_ahead(uint32_t last)
{
	uint32_t n = 0, pages[RUN];

	for (uint32_t p = last + 1; p < rom_pages && n < RUN; p++) {
		bool staged;

		if (resident(p)) {
			continue;
		}
		k_mutex_lock(&state_lock, K_FOREVER);
		staged = find_slot(p) >= 0;
		k_mutex_unlock(&state_lock);
		if (!staged) {
			pages[n++] = p;
		}
	}
	if (n == 0) {
		return;
	}
	k_mutex_lock(&state_lock, K_FOREVER);
	want.n = n;
	memcpy(want.page, pages, n * sizeof(pages[0]));
	k_mutex_unlock(&state_lock);
	k_sem_give(&kick);
}

int rom_page_read(uint32_t page, uint8_t *dst)
{
	uint32_t n = 1;

	if (take_staged(page, dst)) {
		gbsp_rom_hits++;
		ask_ahead(page + RUN - 1);
		return 0;
	}
	/* not staged: this page and the ones after it in one read */
	while (n < RUN && page + n < rom_pages && !resident(page + n)) {
		n++;
	}
	stage_fetch(page, n);
	if (!take_staged(page, dst)) {
		/* the read failed: the page by itself, straight to its place */
		k_mutex_lock(&file_lock, K_FOREVER);
		if (fs_seek(file, (off_t)page * PAGE_BYTES, FS_SEEK_SET) != 0 ||
		    fs_read(file, dst, PAGE_BYTES) != PAGE_BYTES) {
			k_mutex_unlock(&file_lock);
			return -1;
		}
		k_mutex_unlock(&file_lock);
	}
	ask_ahead(page + n - 1);

	return 0;
}
