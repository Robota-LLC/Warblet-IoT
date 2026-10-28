/* STM32WBA65 flash uses 8 KB pages and 16-byte writes.
 * The 2 MB device has two 1 MB banks; the last page is in bank 2. */

#include "flashcfg.h"
#include "board.h"

#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu

#define CFG_MAGIC  0x31425743u   /* bytes 'C','W','B','1' little-endian */
#define CFG_VER    1u

typedef struct {
  unsigned magic;
  unsigned short ver;
  unsigned short len;
  unsigned crc;
  unsigned rsvd;
} cfg_hdr_t;

static unsigned flash_size_bytes(void)
{
  unsigned kb = *(volatile unsigned short *)FLASHSIZE_BASE;
  if (kb == 0xFFFFu || kb == 0u) { kb = 2048u; }   /* blank/engineering part */
  return kb * 1024u;
}

unsigned cfg_page_addr(void)
{
  return (unsigned)FLASH_BASE_NS + flash_size_bytes() - CFG_PAGE_SIZE;
}

static unsigned crc32(const unsigned char *d, unsigned n)
{
  unsigned c = 0xFFFFFFFFu;
  for (unsigned i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) {
      c = (c >> 1) ^ (0xEDB88320u & (unsigned)(-(int)(c & 1u)));
    }
  }
  return ~c;
}

/* The one RAM copy of the stored record. */
static char     s_json[CFG_MAX_PAYLOAD + 1];
static unsigned s_len;

const char *cfg_json(unsigned *len)
{
  if (s_len == 0u) { return 0; }
  if (len) { *len = s_len; }
  return s_json;
}

/* The bank the config page is in: bank 2 on the 2 Mbyte dual-bank parts. */
static unsigned cfg_bank(void)
{
  return (flash_size_bytes() > 0x100000u) ? 1u : 0u;
}

/* Power lost while the page was being written can leave a quadword whose
 * ECC cannot correct it. Reading it sets ECCD in FLASH_ECCR and raises the
 * NMI, which ST's HAL handles there (HAL_FLASHEx_ECCD_IRQHandler: check
 * ECCD, then clear it by writing 1). One raised while cfg_load() copies the
 * page, in the page's own bank, marks the copy unreadable and the load goes
 * on; any other NMI stops the board, as before. */
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

int cfg_load(void)
{
  const cfg_hdr_t *h = (const cfg_hdr_t *)cfg_page_addr();
  const unsigned char *p = (const unsigned char *)(cfg_page_addr() + sizeof(cfg_hdr_t));
  cfg_hdr_t hdr;
  int       ok;

  s_len = 0u;

  /* Copy the record inside the window the NMI handler checks. The barriers
   * keep the copy inside it and let any NMI it raised be taken first. */
  s_unreadable = 0;
  s_reading    = 1;
  __DSB();
  hdr = *h;
  ok = hdr.magic == CFG_MAGIC && hdr.ver == CFG_VER &&
       hdr.len != 0u && hdr.len <= CFG_MAX_PAYLOAD;
  for (unsigned i = 0; ok && i < hdr.len; i++) { s_json[i] = (char)p[i]; }
  __DSB();
  __ISB();
  s_reading = 0;

  /* A damaged page is no config: the board waits for USB setup, and the
   * next push erases and rewrites the page. */
  if (s_unreadable) { return -2; }
  if (!ok || crc32((const unsigned char *)s_json, hdr.len) != hdr.crc) { return -1; }

  s_json[hdr.len] = '\0';
  s_len = hdr.len;
  return 0;
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

static int flash_erase_cfg_page(void)
{
  unsigned total_pages = flash_size_bytes() / CFG_PAGE_SIZE;
  unsigned page = total_pages - 1u;
  unsigned bker = 0u;

  /* Dual bank on the 2 Mbyte devices: the top page is page (n/2 - 1) of bank 2. */
  if (flash_size_bytes() > 0x100000u) {
    bker = 1u;
    page = (total_pages / 2u) - 1u;
  }

  flash_wait();
  flash_clear_err();

  unsigned cr = FLASH_NSCR1_PER | ((page << FLASH_NSCR1_PNB_Pos) & FLASH_NSCR1_PNB_Msk);
  if (bker) { cr |= FLASH_NSCR1_BKER; }
  FLASH->NSCR1 = cr;
  FLASH->NSCR1 = cr | FLASH_NSCR1_STRT;
  flash_wait();

  int bad = flash_err();
  FLASH->NSCR1 &= ~(FLASH_NSCR1_PER | FLASH_NSCR1_PNB_Msk | FLASH_NSCR1_BKER);
  return bad ? -1 : 0;
}

/* Program one 128-bit quadword from a 16-byte source buffer. */
static int flash_prog_qw(unsigned addr, const unsigned char *src)
{
  volatile unsigned *dst = (volatile unsigned *)addr;
  unsigned w[4];

  for (int i = 0; i < 4; i++) {
    w[i] = (unsigned)src[i * 4 + 0]        | ((unsigned)src[i * 4 + 1] << 8) |
           ((unsigned)src[i * 4 + 2] << 16) | ((unsigned)src[i * 4 + 3] << 24);
  }

  flash_wait();
  flash_clear_err();
  FLASH->NSCR1 |= FLASH_NSCR1_PG;

  dst[0] = w[0];
  dst[1] = w[1];
  dst[2] = w[2];
  dst[3] = w[3];
  __DSB();
  flash_wait();

  int bad = flash_err();
  if (FLASH->NSSR & FLASH_NSSR_EOP) { FLASH->NSSR = FLASH_NSSR_EOP; }
  FLASH->NSCR1 &= ~FLASH_NSCR1_PG;
  return bad ? -1 : 0;
}

int cfg_store(const char *json, unsigned len)
{
  unsigned char qw[16];
  cfg_hdr_t h;

  if (len == 0u || len > CFG_MAX_PAYLOAD) { return -1; }

  h.magic = CFG_MAGIC;
  h.ver   = CFG_VER;
  h.len   = (unsigned short)len;
  h.crc   = crc32((const unsigned char *)json, len);
  h.rsvd  = 0u;

  unsigned base = cfg_page_addr();

  /* Mask interrupts during erase and programming. */
  unsigned primask = __get_PRIMASK();
  __disable_irq();

  flash_unlock();
  if (flash_erase_cfg_page() != 0) { flash_lock(); __set_PRIMASK(primask); return -1; }

  /* header (exactly one quadword) */
  const unsigned char *hp = (const unsigned char *)&h;
  for (unsigned i = 0; i < 16u; i++) { qw[i] = hp[i]; }
  if (flash_prog_qw(base, qw) != 0) { flash_lock(); __set_PRIMASK(primask); return -1; }

  /* payload, zero-padded up to the quadword boundary */
  for (unsigned off = 0; off < len; off += 16u) {
    for (unsigned i = 0; i < 16u; i++) {
      qw[i] = (off + i < len) ? (unsigned char)json[off + i] : 0u;
    }
    if (flash_prog_qw(base + 16u + off, qw) != 0) { flash_lock(); __set_PRIMASK(primask); return -1; }
  }

  flash_lock();
  __set_PRIMASK(primask);

  /* Read back through cfg_load(): a store that cannot be re-read as what was
   * pushed is a failed store, and the RAM copy is then what the next power
   * cycle reads. */
  if (cfg_load() != 0 || s_len != len) { return -1; }
  for (unsigned i = 0; i < len; i++) { if (s_json[i] != json[i]) { return -1; } }
  return 0;
}
