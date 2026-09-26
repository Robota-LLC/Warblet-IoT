/* flashcfg.h — one config record in the LAST flash page. Erase + write, no wear
 * levelling, no journal: this is a demo store, and the page is rewritten only
 * when a human pushes a config over USB. */
#ifndef FLASHCFG_H
#define FLASHCFG_H

#define CFG_PAGE_SIZE   0x2000u   /* 8 KB, ST device DB 0x4B0 (sector size 0x2000) */
#define CFG_MAX_PAYLOAD 1008u     /* JSON bytes; header(16) + payload <= 1024     */

/* 0 = a valid record was loaded, -1 = nothing stored / corrupt. */
int cfg_load(char *buf, unsigned bufsz, unsigned *len);

/* 0 = stored and verified, -1 = flash refused (caller answers err store-failed). */
int cfg_store(const char *json, unsigned len);

/* Base address of the config page (for logging). */
unsigned cfg_page_addr(void);

#endif /* FLASHCFG_H */
