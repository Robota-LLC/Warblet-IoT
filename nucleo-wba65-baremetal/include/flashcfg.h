/* One config record in the last flash page: erase + write, no wear levelling. */
#ifndef FLASHCFG_H
#define FLASHCFG_H

#define CFG_PAGE_SIZE   0x2000u   /* 8 KB, ST device DB 0x4B0 (sector size 0x2000) */
#define CFG_MAX_PAYLOAD 1008u     /* JSON bytes; header(16) + payload <= 1024     */

/* Read the stored record into this module's RAM copy.
 * 0 = a valid record is now held, -1 = nothing stored / corrupt. */
int cfg_load(void);

/* The RAM copy: the pushed JSON verbatim, NUL-terminated, or 0 when nothing is held. */
const char *cfg_json(unsigned *len);

/* 0 = stored and verified, -1 = flash refused (caller answers err store-failed).
 * A successful store refreshes the RAM copy. */
int cfg_store(const char *json, unsigned len);

/* Base address of the config page (for logging). */
unsigned cfg_page_addr(void);

#endif /* FLASHCFG_H */
