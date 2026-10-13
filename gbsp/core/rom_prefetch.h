#ifndef ROM_PREFETCH_H
#define ROM_PREFETCH_H

#include <stdbool.h>
#include <stdint.h>

struct fs_file_t;

/* Starts the read-ahead for a ROM of `pages` 32 KiB pages in `f`; is_resident(page) tells whether a
 * page is in memory (only called from the thread that calls rom_page_read). */
int rom_prefetch_start(struct fs_file_t *f, uint32_t pages, bool (*is_resident)(uint32_t page));
void rom_prefetch_stop(void);
bool rom_prefetch_active(void);

/* Reads one page into dst (32 KiB): from the staging buffers when it is there, else from the file.
 * Returns 0 on success. Pages after it are brought in the background. */
int rom_page_read(uint32_t page, uint8_t *dst);

extern uint32_t gbsp_rom_hits;

#endif
