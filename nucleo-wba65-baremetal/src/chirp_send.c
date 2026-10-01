/* chirp_send.c — see chirp_send.h. */

#include "chirp_send.h"
#include "chirp_prov.h"
#include "chirp_sign.h"
#include "flashcfg.h"
#include "json.h"
#include "uart.h"

#define SEND_PERIOD_MS 30000u
#define DEFAULT_HOST   "http.warbletiot.com"   /* §Hosts: the no-TLS front door */

/* ------------------------------------------------------------------ state */

static char          s_hwid[64];
static char          s_host[80];
static char          s_cred[96];        /* the token, or the claim code */
static unsigned char s_key[32];
static int           s_have_key;
static int           s_cred_is_claim;
static int           s_have_cred;

static unsigned s_tenths = 200u;        /* the demo reading, in tenths of a degree */
static unsigned s_last;
static int      s_armed;

/* ------------------------------------------------------------------ helpers */

static void put_uint(unsigned v)
{
  char b[12];
  int i = 0;
  if (v == 0u) { uart_putc('0'); return; }
  while (v && i < 12) { b[i++] = (char)('0' + (v % 10u)); v /= 10u; }
  while (i--) { uart_putc(b[i]); }
}

static int b64_val(char c)
{
  if (c >= 'A' && c <= 'Z') { return c - 'A'; }
  if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
  if (c >= '0' && c <= '9') { return c - '0' + 52; }
  if (c == '+' || c == '-') { return 62; }   /* URL-safe alphabet too — the */
  if (c == '/' || c == '_') { return 63; }   /* validator accepts both      */
  return -1;
}

/* chirpval.c has already checked the key's shape; this only decodes. */
static int b64_decode32(const char *s, unsigned n, unsigned char out[32])
{
  unsigned acc = 0u, bits = 0u, o = 0u;

  if (n != 44u) { return -1; }
  for (unsigned i = 0; i < 43u; i++) {
    int v = b64_val(s[i]);
    if (v < 0) { return -1; }
    acc = (acc << 6) | (unsigned)v;
    bits += 6u;
    if (bits >= 8u) {
      bits -= 8u;
      if (o >= 32u) { return -1; }
      out[o++] = (unsigned char)((acc >> bits) & 0xFFu);
    }
  }
  return (o == 32u) ? 0 : -1;
}

/* Copy a top-level JSON string field out of the stored config. */
static int cfg_field(const char *json, unsigned jl, const char *key,
                     char *dst, unsigned dstsz)
{
  js_val v;
  if (!json) { return 0; }
  if (js_obj_get(json, jl, key, &v) != 1 || v.t != JS_STR || v.n == 0u) { return 0; }
  return js_str_copy(&v, dst, dstsz) > 0;
}

/* ------------------------------------------------------------------ level */

/* Nothing else in this firmware branches on the level. */
static unsigned level(void)
{
  if (s_have_key)  { return 2u; }
  if (s_have_cred) { return 1u; }
  return 0u;
}

void chirp_send_reload(void)
{
  const char *json;
  unsigned    jl = 0u;
  js_val      v;

  s_have_cred     = 0;
  s_cred_is_claim = 0;
  s_have_key      = 0;

  chirp_hwid(s_hwid, sizeof s_hwid);

  /* A pushed `host` is an override for a self-hosted core. */
  {
    unsigned i = 0u;
    while (DEFAULT_HOST[i] != '\0' && i + 1u < sizeof s_host) { s_host[i] = DEFAULT_HOST[i]; i++; }
    s_host[i] = '\0';
  }

  json = cfg_json(&jl);
  if (!json) { return; }

  (void)cfg_field(json, jl, "host", s_host, sizeof s_host);

  /* A token, else the claim code that binds the board on first contact
   * (§Claim-code-first onboarding). A push carries at most one. */
  if (cfg_field(json, jl, "token", s_cred, sizeof s_cred)) {
    s_have_cred = 1;
  } else if (cfg_field(json, jl, "claim", s_cred, sizeof s_cred)) {
    s_have_cred     = 1;
    s_cred_is_claim = 1;
  }

  if (js_obj_get(json, jl, "key", &v) == 1 && v.t == JS_STR &&
      b64_decode32(v.p, v.n, s_key) == 0) {
    s_have_key = 1;
  }
}

/* ------------------------------------------------------------------ the frame */

/* Simulated reading in tenths: start at 201, reach 400, then repeat from 200. */
static unsigned next_tenths(void)
{
  s_tenths = (s_tenths >= 400u || s_tenths < 200u) ? 200u : s_tenths + 1u;
  return s_tenths;
}

/* Print HTTP headers with CHIRP^ prefixes and the body as hex.
 * The README's computer relay converts these lines into an HTTP request. */
static void print_http_request(void)
{
  unsigned char body[2];
  char          sig[65];
  unsigned      t = next_tenths();
  unsigned      lv = level();

  body[0] = (unsigned char)(t >> 8);
  body[1] = (unsigned char)(t & 0xFFu);

  uart_puts("CHIRP. uplink L");
  put_uint(lv);
  uart_puts(" temp_c=");
  put_uint(t / 10u);
  uart_putc('.');
  put_uint(t % 10u);
  uart_line("");

  uart_puts("CHIRP^ POST /ingest/");
  uart_puts(s_hwid);
  uart_line(" HTTP/1.1");

  uart_puts("CHIRP^ Host: ");
  uart_line(s_host);

  uart_line("CHIRP^ Content-Type: application/octet-stream");
  uart_line("CHIRP^ Content-Length: 2");

  if (s_have_cred) {
    uart_puts(s_cred_is_claim ? "CHIRP^ X-Chirp-Claim: " : "CHIRP^ X-Chirp-Token: ");
    uart_line(s_cred);
  }

  if (s_have_key) {
    /* Sign the body without a nonce; this image has no wall clock. */
    chirp_sign(s_key, sizeof s_key, body, sizeof body, sig);
    uart_puts("CHIRP^ X-Chirp-Signature: ");
    uart_line(sig);
  }

  uart_puts("CHIRP^ body ");
  {
    static const char H[] = "0123456789abcdef";
    char hex[5];
    hex[0] = H[body[0] >> 4]; hex[1] = H[body[0] & 0xFu];
    hex[2] = H[body[1] >> 4]; hex[3] = H[body[1] & 0xFu];
    hex[4] = '\0';
    uart_line(hex);
  }
}

/* ------------------------------------------------------------------ api */

void chirp_send_init(void)
{
  chirp_send_reload();

  uart_puts("CHIRP. uplink ");
  uart_puts(s_host);
  uart_puts(" level=L");
  put_uint(level());
  uart_line(" (cap L2: no TLS stack on this board)");
}

void chirp_send_tick(unsigned now_ms)
{
  if (!s_armed) { s_armed = 1; s_last = now_ms; return; }
  if ((unsigned)(now_ms - s_last) < SEND_PERIOD_MS) { return; }
  s_last = now_ms;
  print_http_request();
}
