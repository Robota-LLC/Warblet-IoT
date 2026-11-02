/* The settings record in the last two flash pages. A save writes the page not
 * in use and erases the old one last, so a power cut leaves one whole record. */
#ifndef FLASHCFG_H
#define FLASHCFG_H

#define CFG_PAGE_SIZE   0x2000u   /* 8 KB, ST device DB 0x4B0 (sector size 0x2000) */
#define CFG_MAX_PAYLOAD 4080u     /* JSON bytes; header(16) + payload <= 4096     */

/* Read the newest valid record into this module's RAM copy.
 * 0 = a valid record is now held, -1 = nothing stored / corrupt,
 * -2 = no valid record, and a page could not be read (a flash ECC error). */
int cfg_load(void);

/* The RAM copy: the stored JSON, NUL-terminated, or 0 when nothing is held. */
const char *cfg_json(unsigned *len);

/* 0 = stored and read back, -1 = not stored (caller answers err store-failed):
 * the record held before is still the one the next start reads. A successful
 * store refreshes the RAM copy. Call cfg_load() once first. */
int cfg_store(const char *json, unsigned len);

/* The page holding the record now, or the first page when there is none (for logging). */
unsigned cfg_page_addr(void);

#endif /* FLASHCFG_H */
