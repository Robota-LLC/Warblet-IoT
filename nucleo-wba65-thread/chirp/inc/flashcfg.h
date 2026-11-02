/* flashcfg.h — the config record in the LAST two flash pages. A save writes
 * the page not in use and erases the old one last, so a power cut leaves one
 * whole record; at start the newest valid one wins. No wear levelling: the
 * pages are rewritten only when a human pushes a config over USB. */
#ifndef FLASHCFG_H
#define FLASHCFG_H

#define CFG_PAGE_SIZE   0x2000u   /* 8 KB flash pages                            */
#define CFG_MAX_PAYLOAD 1008u     /* JSON bytes; header(16) + payload <= 1024     */

/* Read the newest valid record into buf.
 * 0 = a valid record was loaded, -1 = nothing stored / corrupt,
 * -2 = no valid record, and a page could not be read (a flash ECC error). */
int cfg_load(char *buf, unsigned bufsz, unsigned *len);

/* 0 = stored and read back, -1 = not stored (caller answers err store-failed):
 * the record held before is still the one the next start reads.
 * Refused (-1) until cfg_load() has run once: before that, page 0 may hold
 * the only record. */
int cfg_store(const char *json, unsigned len);

/* The page holding the record now, or the first page when there is none (for logging). */
unsigned cfg_page_addr(void);

#endif /* FLASHCFG_H */
