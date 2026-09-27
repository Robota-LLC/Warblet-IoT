#include "chirp_prov.h"
#include "json.h"
#include "chirpval.h"
#include "flashcfg.h"
#include "chirp_send.h"
#include "chirp_sign.h"
#include "uart.h"
#include "dfu.h"
#include "board.h"

/* ------------------------------------------------------------------ small str */

static int s_eq(const char *a, const char *b)
{
  while (*a && *a == *b) { a++; b++; }
  return *a == '\0' && *b == '\0';
}

static int s_starts(const char *s, const char *pfx)
{
  while (*pfx) { if (*s++ != *pfx++) { return 0; } }
  return 1;
}

static void put_hex(unsigned v, int digits)
{
  static const char H[] = "0123456789abcdef";
  for (int i = digits - 1; i >= 0; i--) { uart_putc(H[(v >> (i * 4)) & 0xFu]); }
}

static void put_uint(unsigned v)
{
  char b[12];
  int i = 0;
  if (v == 0u) { uart_putc('0'); return; }
  while (v && i < 12) { b[i++] = (char)('0' + (v % 10u)); v /= 10u; }
  while (i--) { uart_putc(b[i]); }
}

/* ------------------------------------------------------------------ hardware id */

/* The pushed `hwid` if there is one, else the low 24 bits of UID word 0
 * (the per-die part of the unique id). */
unsigned chirp_hwid(char *out, unsigned outsz)
{
  static const char H[] = "0123456789abcdef";
  static const char pfx[] = "wba65-demo-";
  const char *json;
  unsigned    jl = 0u, n = 0u, uid0;
  js_val      v;

  if (outsz == 0u) { return 0u; }

  json = cfg_json(&jl);
  if (json && js_obj_get(json, jl, "hwid", &v) == 1 && v.t == JS_STR && v.n > 0u) {
    int copied = js_str_copy(&v, out, outsz);
    if (copied > 0) { return (unsigned)copied; }
  }

  uid0 = *(volatile const unsigned *)UID_BASE;
  while (pfx[n] != '\0' && n + 1u < outsz) { out[n] = pfx[n]; n++; }
  for (int i = 5; i >= 0 && n + 1u < outsz; i--) {
    out[n++] = H[(uid0 >> (i * 4)) & 0xFu];
  }
  out[n] = '\0';
  return n;
}

static void put_hwid(void)
{
  char id[64];
  unsigned n = chirp_hwid(id, sizeof id);
  uart_write(id, n);
}

/* ------------------------------------------------------------------ state */

static char     s_line[CHIRP_LINE_MAX + 1];
static unsigned s_fill;
static int      s_overflow;

/* ------------------------------------------------------------------ config summary */

static void put_key_fp(const char *secret, unsigned n)
{
  char fp[9];

  chirp_fingerprint(secret, n, fp);
  uart_puts("sha256:");
  uart_puts(fp);
}

static void summarize(const char *json, unsigned len)
{
  js_val v, t;

  uart_puts("CHIRP. cfg");

  if (js_obj_get(json, len, "host", &v) == 1 && v.t == JS_STR) {
    uart_puts(" host=");
    uart_write(v.p, v.n);
  }
  if (js_obj_get(json, len, "ssid", &v) == 1 && v.t == JS_STR) {
    uart_puts(" ssid=");
    uart_write(v.p, v.n);
  }

  /* Report whether a signing key is stored without printing it. */
  if (js_obj_get(json, len, "key", &v) == 1 && v.t == JS_STR) {
    uart_puts(" key=stored");
  }

  if (js_obj_get(json, len, "thread", &t) == 1 && t.t == JS_OBJ) {
    if (js_obj_get(t.p, t.n, "dataset", &v) == 1 && v.t == JS_STR) {
      uart_puts(" thread=dataset(");
      put_uint(v.n / 2u);
      uart_puts("B) fp=");
      put_key_fp(v.p, v.n);
    } else {
      if (js_obj_get(t.p, t.n, "name", &v) == 1 && v.t == JS_STR) {
        uart_puts(" thread=");
        uart_write(v.p, v.n);
      }
      if (js_obj_get(t.p, t.n, "key", &v) == 1 && v.t == JS_STR) {
        uart_puts(" key=");
        put_key_fp(v.p, v.n);
      }
      if (js_obj_get(t.p, t.n, "channel", &v) == 1 && v.t == JS_NUM) {
        long ch = 0;
        if (js_num_int(&v, &ch) == 0) { uart_puts(" ch="); put_uint((unsigned)ch); }
      }
      if (js_obj_get(t.p, t.n, "panid", &v) == 1 && v.t == JS_STR) {
        uart_puts(" panid=");
        uart_write(v.p, v.n);
      }
    }
    uart_puts(" (not joined: no radio stack in this demo)");
  }
  uart_line("");
}

/* ------------------------------------------------------------------ line handling */

/* Announce dataset parsing and signing support, plus the demo slug.
 * Omit radios because this image does not drive a radio. */
static void announce(void)
{
  uart_puts("CHIRP! v=2 hw=");
  put_hwid();
  uart_line(" sig=1 t=demo-nucleo-wba65");
}

/* Merge scratch, sized like the store's record. */
static char s_merged[CFG_MAX_PAYLOAD + 1];

static void handle_config(char *json, unsigned len)
{
  const char *stored;
  unsigned    slen = 0u;
  int         mlen;

  /* Field rules are judged on the push alone. */
  const char *err = chirp_validate_fields(json, len);
  if (err) {
    uart_puts("CHIRP= err ");
    uart_line(err);
    return;
  }

  stored = cfg_json(&slen);
  mlen = chirp_merge(stored, slen, json, len, s_merged, sizeof s_merged);
  if (mlen < 0) {                       /* our page holds 1008 JSON bytes */
    uart_line("CHIRP= err store-failed");
    return;
  }

  /* Completeness is judged on the merged result. */
  err = chirp_validate(s_merged, (unsigned)mlen);
  if (err) {
    uart_puts("CHIRP= err ");
    uart_line(err);
    return;
  }

  if (cfg_store(s_merged, (unsigned)mlen) != 0) {
    uart_line("CHIRP= err store-failed");
    return;
  }
  uart_line("CHIRP= ok");
  summarize(s_merged, (unsigned)mlen);
  /* Re-read the credentials now (contract §Elevation): the next uplink is
   * signed with no reboot. */
  chirp_send_reload();
}

static void handle_line(char *line, unsigned len)
{
  /* Strip trailing whitespace, including CR from CRLF input. */
  while (len && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) {
    line[--len] = '\0';
  }
  if (len == 0u) { return; }

  /* Lines that are not CHIRP are ordinary console traffic: ignore them. */
  if (!s_starts(line, "CHIRP")) { return; }

  if (s_eq(line, "CHIRP?")) { announce(); return; }

  if (s_starts(line, "CHIRP+ ")) {
    char *json = line + 7;
    unsigned jl = len - 7u;
    while (jl && (*json == ' ' || *json == '\t')) { json++; jl--; }
    if (jl == 0u) { uart_line("CHIRP= err bad-json"); return; }
    handle_config(json, jl);
    return;
  }

  if (s_eq(line, "CHIRP~ dfu")) {
    uart_line("CHIRP~ ok dfu");
    uart_line("CHIRP. jumping to system bootloader at 0x0BF90000");
    dfu_enter_system_bootloader();
    return;                              /* unreachable */
  }

  uart_line("CHIRP= err unsupported");
}

/* ------------------------------------------------------------------ api */

void chirp_prov_init(void)
{
  unsigned len = 0;

  uart_line("");
  uart_puts("CHIRP. chirpwba-demo boot hw=");
  put_hwid();
  uart_puts(" cfgpage=0x");
  put_hex(cfg_page_addr(), 8);
  uart_line("");

  if (cfg_load() == 0) {
    const char *json = cfg_json(&len);
    summarize(json, len);
  } else {
    uart_line("CHIRP. cfg none");
  }
  uart_line("CHIRP. ready (send CHIRP? / CHIRP+ <json> / CHIRP~ dfu)");
}

void chirp_prov_poll(void)
{
  int c;
  while ((c = uart_getc()) >= 0) {
    if (c == '\n') {
      if (s_overflow) {
        uart_line("CHIRP= err bad-json");     /* line longer than we accept */
      } else {
        s_line[s_fill] = '\0';
        handle_line(s_line, s_fill);
      }
      s_fill = 0;
      s_overflow = 0;
      continue;
    }
    if (s_fill < CHIRP_LINE_MAX) { s_line[s_fill++] = (char)c; }
    else                         { s_overflow = 1; }
  }
}
