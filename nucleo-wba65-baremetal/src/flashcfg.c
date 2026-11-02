/* The settings record, kept in the last two 8 KB pages of flash (bank 2 of
 * the 2 MB part; STM32WBA65 flash is written 16 bytes at a time). A save
 * writes the new record to the page not in use, reads it back, and only then
 * erases the old page. At start the newest valid record wins. So a power cut
 * at any moment leaves one whole record to read: the old one or the new one. */

#include "flashcfg.h"
#include "board.h"

#define CFG_MAGIC  0x31425743u   /* bytes 'C','W','B','1' little-endian */
#define CFG_VER    2u            /* two pages and a sequence number (1, one page, is not read) */

/* A record's first 16 bytes. The CRC covers seq and len as well as the
 * payload, so a header that a power cut tore cannot pass for a newer record. */
typedef struct {
  unsigned       magic;
  unsigned short ver;
  unsigned short len;
  unsigned       seq;      /* one more than the record it replaced */
  unsigned       crc;      /* CRC-32 of seq, len and the payload */
} cfg_hdr_t;

_Static_assert(sizeof(cfg_hdr_t) == 16u, "the header is one 16-byte flash write");
_Static_assert(sizeof(cfg_hdr_t) + CFG_MAX_PAYLOAD <= CFG_PAGE_SIZE, "a record fits in a page");

/* The flash, as the store uses it: at the end of this file. The host test
 * (tools/valtest.c) brings its own, which can fail or lose power at any step. */
unsigned cfg_flash_page(unsigned i);                             /* page 0 or 1 */
int      cfg_flash_read(unsigned addr, void *dst, unsigned n);   /* 0, or -2: an ECC error */
int      cfg_flash_erase(unsigned i);                            /* 0, or -1 */
int      cfg_flash_write(unsigned addr, const unsigned char qw[16]);  /* 0, or -1 */

static unsigned crc32_add(unsigned c, const unsigned char *d, unsigned n)
{
  for (unsigned i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) {
      c = (c >> 1) ^ (0xEDB88320u & (unsigned)(-(int)(c & 1u)));
    }
  }
  return c;
}

/* The CRC of a record's seq and len, which its payload then continues. */
static unsigned head_crc(unsigned seq, unsigned len)
{
  unsigned char b[6];

  for (unsigned i = 0u; i < 4u; i++) { b[i] = (unsigned char)(seq >> (8u * i)); }
  b[4] = (unsigned char)len;
  b[5] = (unsigned char)(len >> 8);
  return crc32_add(0xFFFFFFFFu, b, sizeof b);
}

/* The one RAM copy of the stored record, and where it came from. */
static char     s_json[CFG_MAX_PAYLOAD + 1];
static unsigned s_len;
static int      s_page = -1;     /* the page with the newest valid record, or -1 */
static unsigned s_seq;           /* that record's seq */

const char *cfg_json(unsigned *len)
{
  if (s_len == 0u) { return 0; }
  if (len) { *len = s_len; }
  return s_json;
}

unsigned cfg_page_addr(void)
{
  return cfg_flash_page(s_page == 1 ? 1u : 0u);
}

/* Is page i one whole record? 1 = yes (its header in *h), 0 = no, -2 = it
 * could not be read. With `to`, the payload is copied there as it is checked. */
static int read_record(unsigned i, cfg_hdr_t *h, char *to)
{
  unsigned      base = cfg_flash_page(i), c, off, n;
  unsigned char chunk[64];

  if (cfg_flash_read(base, h, sizeof *h) != 0) { return -2; }
  if (h->magic != CFG_MAGIC || h->ver != CFG_VER || h->len == 0u || h->len > CFG_MAX_PAYLOAD) {
    return 0;
  }
  c = head_crc(h->seq, h->len);
  for (off = 0u; off < h->len; off += n) {
    n = (h->len - off < sizeof chunk) ? h->len - off : (unsigned)sizeof chunk;
    if (cfg_flash_read(base + sizeof *h + off, chunk, n) != 0) { return -2; }
    c = crc32_add(c, chunk, n);
    for (unsigned k = 0u; to && k < n; k++) { to[off + k] = (char)chunk[k]; }
  }
  return ~c == h->crc ? 1 : 0;
}

/* Does page i hold exactly this record, as the next start would read it? */
static int holds(unsigned i, unsigned seq, const char *json, unsigned len)
{
  cfg_hdr_t     h;
  unsigned char chunk[64];
  unsigned      off, n;

  if (read_record(i, &h, 0) != 1 || h.seq != seq || h.len != len) { return 0; }
  for (off = 0u; off < len; off += n) {
    n = (len - off < sizeof chunk) ? len - off : (unsigned)sizeof chunk;
    if (cfg_flash_read(cfg_flash_page(i) + sizeof h + off, chunk, n) != 0) { return 0; }
    for (unsigned k = 0u; k < n; k++) { if (chunk[k] != (unsigned char)json[off + k]) { return 0; } }
  }
  return 1;
}

int cfg_load(void)
{
  cfg_hdr_t h[2];
  int       got[2], newest;

  s_len  = 0u;
  s_page = -1;
  got[0] = read_record(0u, &h[0], 0);
  got[1] = read_record(1u, &h[1], 0);

  /* The newest valid record wins (seq may count on past 2^32), then the other. */
  newest = (got[1] == 1 && (got[0] != 1 || (int)(h[1].seq - h[0].seq) > 0)) ? 1 : 0;
  for (int k = 0; k < 2; k++) {
    unsigned p = (unsigned)(k == 0 ? newest : 1 - newest);
    if (got[p] == 1 && read_record(p, &h[p], s_json) == 1) {
      s_json[h[p].len] = '\0';
      s_len  = h[p].len;
      s_page = (int)p;
      s_seq  = h[p].seq;
      return 0;
    }
  }
  /* No record: the board waits for USB setup, and the next push writes one. */
  return (got[0] == -2 || got[1] == -2) ? -2 : -1;
}

int cfg_store(const char *json, unsigned len)
{
  unsigned char qw[16];
  cfg_hdr_t     h;
  unsigned      t = (s_page == 0) ? 1u : 0u;   /* the page not in use */
  unsigned      base = cfg_flash_page(t), off, i;

  if (len == 0u || len > CFG_MAX_PAYLOAD) { return -1; }

  h.magic = CFG_MAGIC;
  h.ver   = CFG_VER;
  h.len   = (unsigned short)len;
  h.seq   = (s_page >= 0) ? s_seq + 1u : 1u;
  h.crc   = ~crc32_add(head_crc(h.seq, len), (const unsigned char *)json, len);

  /* 1. The new record goes to the page not in use: erase it, write the
   *    payload (zero-padded to 16 bytes), then the header, since the header
   *    is what makes it a record. */
  if (cfg_flash_erase(t) != 0) { return -1; }
  for (off = 0u; off < len; off += 16u) {
    for (i = 0u; i < 16u; i++) { qw[i] = (off + i < len) ? (unsigned char)json[off + i] : 0u; }
    if (cfg_flash_write(base + 16u + off, qw) != 0) { return -1; }
  }
  for (i = 0u; i < 16u; i++) { qw[i] = ((const unsigned char *)&h)[i]; }

  /* 2. Read it back as the next start would. A record that failed to write,
   *    or does not read back as what was pushed, is erased again, so the old
   *    record stays the newest and the next start reads it. */
  if (cfg_flash_write(base, qw) != 0 || !holds(t, h.seq, json, len)) {
    (void)cfg_flash_erase(t);
    return -1;
  }

  /* 3. Only now does the old page go. Should that fail, the new record is
   *    still the newest, so the next start reads it all the same. */
  (void)cfg_flash_erase(1u - t);

  for (i = 0u; i < len; i++) { s_json[i] = json[i]; }
  s_json[len] = '\0';
  s_len  = len;
  s_page = (int)t;
  s_seq  = h.seq;
  return 0;
}

#ifndef CFG_FAKE_FLASH
/* ------------------------------------------------------------------ the flash */

#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu

static unsigned flash_size_bytes(void)
{
  unsigned kb = *(volatile unsigned short *)FLASHSIZE_BASE;
  if (kb == 0xFFFFu || kb == 0u) { kb = 2048u; }   /* blank/engineering part */
  return kb * 1024u;
}

/* The last two pages, the linker script's CONFIG region. */
unsigned cfg_flash_page(unsigned i)
{
  return (unsigned)FLASH_BASE_NS + flash_size_bytes() - (2u - i) * CFG_PAGE_SIZE;
}

/* The bank the store is in: bank 2 on the 2 Mbyte dual-bank parts. */
static unsigned cfg_bank(void)
{
  return (flash_size_bytes() > 0x100000u) ? 1u : 0u;
}

/* Power lost while a page was being written can leave a quadword whose ECC
 * cannot correct it. Reading it sets ECCD in FLASH_ECCR and raises the NMI,
 * which ST's HAL handles there (HAL_FLASHEx_ECCD_IRQHandler: check ECCD, then
 * clear it by writing 1). One raised while cfg_flash_read() copies from the
 * store's own bank marks the read unreadable and the read goes on; any other
 * NMI stops the board, as before. */
static volatile int s_reading, s_unreadable;

void NMI_Handler(void)
{
  unsigned eccr = FLASH->ECCR;

  if (s_reading && (eccr & FLASH_ECCR_ECCD) != 0u && (eccr & FLASH_ECCR_SYSF_ECC) == 0u &&
      ((eccr & FLASH_ECCR_BK_ECC) != 0u) == (cfg_bank() == 1u)) {
    s_unreadable = 1;
    FLASH->ECCR |= FLASH_ECCR_ECCD;
    return;
  }
  for (;;) { __WFI(); }                  /* a debugger can inspect it here */
}

int cfg_flash_read(unsigned addr, void *dst, unsigned n)
{
  const volatile unsigned char *p = (const volatile unsigned char *)addr;
  unsigned char                *d = (unsigned char *)dst;

  /* Copy inside the window the NMI handler checks. The barriers keep the
   * copy inside it and let any NMI it raised be taken first. */
  s_unreadable = 0;
  s_reading    = 1;
  __DSB();
  for (unsigned i = 0u; i < n; i++) { d[i] = p[i]; }
  __DSB();
  __ISB();
  s_reading = 0;
  return s_unreadable ? -2 : 0;
}

static void flash_wait(void)
{
  while (FLASH->NSSR & FLASH_NSSR_BSY) { }
}

static int flash_err(void)
{
  return (FLASH->NSSR & (FLASH_NSSR_OPERR | FLASH_NSSR_PROGERR | FLASH_NSSR_WRPERR |
                         FLASH_NSSR_PGAERR | FLASH_NSSR_SIZERR | FLASH_NSSR_PGSERR)) != 0u;
}

static void flash_clear_err(void)
{
  FLASH->NSSR = FLASH_NSSR_EOP | FLASH_NSSR_OPERR | FLASH_NSSR_PROGERR | FLASH_NSSR_WRPERR |
                FLASH_NSSR_PGAERR | FLASH_NSSR_SIZERR | FLASH_NSSR_PGSERR;
}

static void flash_unlock(void)
{
  if (FLASH->NSCR1 & FLASH_NSCR1_LOCK) {
    FLASH->NSKEYR = FLASH_KEY1;
    FLASH->NSKEYR = FLASH_KEY2;
  }
}

static void flash_lock(void)
{
  FLASH->NSCR1 |= FLASH_NSCR1_LOCK;
}

int cfg_flash_erase(unsigned i)
{
  unsigned total_pages = flash_size_bytes() / CFG_PAGE_SIZE;
  unsigned page = total_pages - 2u + i;
  unsigned bker = 0u, primask, cr;
  int      bad;

  /* Dual bank on the 2 Mbyte devices: the last pages are pages n/2 - 2 and
   * n/2 - 1 of bank 2. */
  if (flash_size_bytes() > 0x100000u) {
    bker = 1u;
    page = (total_pages / 2u) - 2u + i;
  }

  /* Mask interrupts while the flash is busy. */
  primask = __get_PRIMASK();
  __disable_irq();
  flash_unlock();
  flash_wait();
  flash_clear_err();

  cr = FLASH_NSCR1_PER | ((page << FLASH_NSCR1_PNB_Pos) & FLASH_NSCR1_PNB_Msk);
  if (bker) { cr |= FLASH_NSCR1_BKER; }
  FLASH->NSCR1 = cr;
  FLASH->NSCR1 = cr | FLASH_NSCR1_STRT;
  flash_wait();

  bad = flash_err();
  FLASH->NSCR1 &= ~(FLASH_NSCR1_PER | FLASH_NSCR1_PNB_Msk | FLASH_NSCR1_BKER);
  flash_lock();
  __set_PRIMASK(primask);
  return bad ? -1 : 0;
}

/* Program one 128-bit quadword from a 16-byte source buffer. */
int cfg_flash_write(unsigned addr, const unsigned char qw[16])
{
  volatile unsigned *dst = (volatile unsigned *)addr;
  unsigned w[4], primask;
  int      bad;

  for (int i = 0; i < 4; i++) {
    w[i] = (unsigned)qw[i * 4 + 0]        | ((unsigned)qw[i * 4 + 1] << 8) |
           ((unsigned)qw[i * 4 + 2] << 16) | ((unsigned)qw[i * 4 + 3] << 24);
  }

  primask = __get_PRIMASK();
  __disable_irq();
  flash_unlock();
  flash_wait();
  flash_clear_err();
  FLASH->NSCR1 |= FLASH_NSCR1_PG;

  dst[0] = w[0];
  dst[1] = w[1];
  dst[2] = w[2];
  dst[3] = w[3];
  __DSB();
  flash_wait();

  bad = flash_err();
  if (FLASH->NSSR & FLASH_NSSR_EOP) { FLASH->NSSR = FLASH_NSSR_EOP; }
  FLASH->NSCR1 &= ~FLASH_NSCR1_PG;
  flash_lock();
  __set_PRIMASK(primask);
  return bad ? -1 : 0;
}
#endif /* CFG_FAKE_FLASH */
