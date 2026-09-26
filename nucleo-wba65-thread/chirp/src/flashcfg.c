/* STM32WBA65 flash store: 8 KB pages, 16-byte writes, page declared by the linker.
 * A slot tag records bank geometry: a mismatch reads as no config, and writes
 * with SWAP_BANK set are refused. One page, erase-then-write, no wear levelling:
 * a power cut mid-store can lose the config. */

#include "flashcfg.h"
#include "stm32wbaxx.h"

/* Use HAL definitions when available, with local fallbacks otherwise. */
#ifndef FLASH_KEY1
#define FLASH_KEY1 0x45670123u
#endif
#ifndef FLASH_KEY2
#define FLASH_KEY2 0xCDEF89ABu
#endif

#define CFG_MAGIC  0x31425743u   /* bytes 'C','W','B','1' little-endian */
#define CFG_VER    2u            /* Config record format with a bank-geometry tag */

/* Exported by ld/thread_chirp.ld. Only the addresses of these matter. */
extern char _chirp_cfg_start[];
extern char _chirp_cfg_size[];

typedef struct {
  unsigned       magic;
  unsigned short ver;
  unsigned short len;
  unsigned       crc;
  unsigned       slot;      /* cfg_slot_tag() at write time */
} cfg_hdr_t;

/* Exactly one quadword, so the header is a single programming burst. */
_Static_assert(sizeof(cfg_hdr_t) == 16u, "cfg header must be one 128-bit quadword");

unsigned cfg_page_addr(void)
{
  return (unsigned)(unsigned long)_chirp_cfg_start;
}

static unsigned cfg_page_size(void)
{
  return (unsigned)(unsigned long)_chirp_cfg_size;
}

static unsigned flash_size_bytes(void)
{
  unsigned kb = *(volatile unsigned short *)FLASHSIZE_BASE;
  if (kb == 0xFFFFu || kb == 0u) { kb = 2048u; }   /* blank/engineering part */
  return kb * 1024u;
}

static int flash_is_dual_bank(void)
{
  return (FLASH->OPTR & FLASH_OPTR_DUAL_BANK) != 0u;
}

static int flash_banks_swapped(void)
{
  return (FLASH->OPTR & FLASH_OPTR_SWAP_BANK) != 0u;
}

/* Which bank the store's address falls in (0 = bank 1, 1 = bank 2). */
static unsigned cfg_bank(void)
{
  unsigned off = cfg_page_addr() - (unsigned)FLASH_BASE_NS;

  if (!flash_is_dual_bank()) { return 0u; }
  return (off >= (flash_size_bytes() / 2u)) ? 1u : 0u;
}

/* Identity of the physical page, stored so a later boot can check the geometry. */
static unsigned cfg_slot_tag(void)
{
  return cfg_bank() | (flash_banks_swapped() ? 2u : 0u) |
         (flash_is_dual_bank() ? 4u : 0u);
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

int cfg_load(char *buf, unsigned bufsz, unsigned *len)
{
  const cfg_hdr_t *h = (const cfg_hdr_t *)cfg_page_addr();

  if (h->magic != CFG_MAGIC || h->ver != CFG_VER) { return -1; }

  /* Written under a different bank geometry: not our record. Report none; do not erase. */
  if (h->slot != cfg_slot_tag()) { return -1; }

  if (h->len == 0u || h->len > CFG_MAX_PAYLOAD || h->len >= bufsz) { return -1; }

  const unsigned char *p = (const unsigned char *)(cfg_page_addr() + sizeof(cfg_hdr_t));
  if (crc32(p, h->len) != h->crc) { return -1; }

  for (unsigned i = 0; i < h->len; i++) { buf[i] = (char)p[i]; }
  buf[h->len] = '\0';
  *len = h->len;
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
  unsigned off  = cfg_page_addr() - (unsigned)FLASH_BASE_NS;
  unsigned bker = cfg_bank();
  unsigned page;

  /* Page index *within its bank*, derived from the linker-declared address. */
  if (flash_is_dual_bank()) {
    page = (off % (flash_size_bytes() / 2u)) / cfg_page_size();
  } else {
    page = off / cfg_page_size();
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
  unsigned primask;

  for (int i = 0; i < 4; i++) {
    w[i] = (unsigned)src[i * 4 + 0]        | ((unsigned)src[i * 4 + 1] << 8) |
           ((unsigned)src[i * 4 + 2] << 16) | ((unsigned)src[i * 4 + 3] << 24);
  }

  flash_wait();
  flash_clear_err();
  FLASH->NSCR1 |= FLASH_NSCR1_PG;

  /* Mask interrupts for the four stores that program one quadword. */
  primask = __get_PRIMASK();
  __disable_irq();
  dst[0] = w[0];
  dst[1] = w[1];
  dst[2] = w[2];
  dst[3] = w[3];
  __DSB();
  __set_PRIMASK(primask);

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

  /* With SWAP_BANK set, which physical page BKER/PNB erases is uncertain, and
   * the wrong one is firmware. */
  if (flash_banks_swapped()) { return -1; }

  /* Payload plus its one-quadword header must fit the reserved page. */
  if (sizeof(cfg_hdr_t) + ((len + 15u) & ~15u) > cfg_page_size()) { return -1; }

  h.magic = CFG_MAGIC;
  h.ver   = CFG_VER;
  h.len   = (unsigned short)len;
  h.crc   = crc32((const unsigned char *)json, len);
  h.slot  = cfg_slot_tag();

  unsigned base = cfg_page_addr();

  flash_unlock();
  if (flash_erase_cfg_page() != 0) { flash_lock(); return -1; }

  /* header (exactly one quadword) */
  const unsigned char *hp = (const unsigned char *)&h;
  for (unsigned i = 0; i < 16u; i++) { qw[i] = hp[i]; }
  if (flash_prog_qw(base, qw) != 0) { flash_lock(); return -1; }

  /* payload, zero-padded up to the quadword boundary */
  for (unsigned off = 0; off < len; off += 16u) {
    for (unsigned i = 0; i < 16u; i++) {
      qw[i] = (off + i < len) ? (unsigned char)json[off + i] : 0u;
    }
    if (flash_prog_qw(base + 16u + off, qw) != 0) { flash_lock(); return -1; }
  }

  flash_lock();

  /* Read back: a store that cannot be re-read is a failed store. */
  const cfg_hdr_t *v = (const cfg_hdr_t *)base;
  if (v->magic != CFG_MAGIC || v->len != h.len || v->crc != h.crc || v->slot != h.slot) {
    return -1;
  }
  if (crc32((const unsigned char *)(base + sizeof(cfg_hdr_t)), len) != h.crc) { return -1; }
  return 0;
}
