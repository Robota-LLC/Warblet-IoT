/* chirp_send.c — see chirp_send.h. */

#include "chirp_send.h"
#include "chirp_prov.h"
#include "chirp_sign.h"
#include "chirpval.h"
#include "flashcfg.h"
#include "json.h"
#include "uart.h"

#define SEND_PERIOD_MS 30000u
#define DEFAULT_HOST   "http.warbletiot.com"   /* §Hosts: the no-TLS front door */

/* ------------------------------------------------------------------ state */

static char          s_hwid[CHIRP_HWID_SIZE];
static const char   *s_host;            /* DEFAULT_HOST, or s_host_buf */
static char          s_host_buf[CHIRP_HOST_MAX + 1u];
static char          s_cred[CHIRP_CRED_MAX + 1u];  /* the token, or the claim code */
static unsigned char s_key[32];
static int           s_have_key;
static int           s_cred_is_claim;
static int           s_have_cred;

static unsigned s_tenths = 200u;        /* the demo reading, in tenths of a degree */
static unsigned s_last;
static int      s_armed;
static int      s_told_idle;            /* "nothing to send with" has been said */

/* ------------------------------------------------------------------ helpers */

static int b64_val(char c)
{
  if (c >= 'A' && c <= 'Z') { return c - 'A'; }
  if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
  if (c >= '0' && c <= '9') { return c - '0' + 52; }
  if (c == '+') { return 62; }
  if (c == '/') { return 63; }
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

/* A top-level string field of the stored config into dst, decoded: what has
 * to fit is the decoded text (a credential's " and \ are stored escaped, at
 * twice the length), so js_str_copy() decides. Returns its length; 0 when it
 * is absent, empty, or too long for dst once decoded, and dst is then "". */
static unsigned cfg_field(const char *json, unsigned jl, const char *key,
                          char *dst, unsigned dstsz)
{
  js_val v;
  int    n;

  dst[0] = '\0';
  if (!json || js_obj_get(json, jl, key, &v) != 1 || v.t != JS_STR) { return 0u; }
  n = js_str_copy(&v, dst, dstsz);
  if (n <= 0) { dst[0] = '\0'; return 0u; }
  return (unsigned)n;
}

/* ------------------------------------------------------------------ level */

/* Nothing else in this firmware branches on the level. With no credential
 * nothing is sent at all: this demo does not send at Open. */
static unsigned level(void)
{
  if (s_have_key)  { return 2u; }
  if (s_have_cred) { return 1u; }
  return 0u;
}

void chirp_send_reload(void)
{
  const char *json;
  unsigned    jl = 0u, kn;
  char        key_b64[CHIRP_KEY_LEN + 1u];

  s_have_cred     = 0;
  s_cred_is_claim = 0;
  s_have_key      = 0;
  s_told_idle     = 0;

  chirp_hwid(s_hwid, sizeof s_hwid);

  json = cfg_json(&jl);

  /* A pushed `host` is an override for a self-hosted core. */
  s_host = cfg_field(json, jl, "host", s_host_buf, sizeof s_host_buf) ? s_host_buf
                                                                       : DEFAULT_HOST;
  if (!json) { return; }

  /* A token, else the claim code that binds the board on first contact
   * (§Claim-code-first onboarding). A push carries at most one. */
  if (cfg_field(json, jl, "token", s_cred, sizeof s_cred)) {
    s_have_cred = 1;
  } else if (cfg_field(json, jl, "claim", s_cred, sizeof s_cred)) {
    s_have_cred     = 1;
    s_cred_is_claim = 1;
  }

  kn = cfg_field(json, jl, "key", key_b64, sizeof key_b64);
  if (kn > 0u && b64_decode32(key_b64, kn, s_key) == 0) {
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
  uart_put_uint(lv);
  uart_puts(" temp_c=");
  uart_put_uint(t / 10u);
  uart_putc('.');
  uart_put_uint(t % 10u);
  uart_line("");

  uart_puts("CHIRP^ POST /ingest/");
  uart_puts(s_hwid);
  uart_line(" HTTP/1.1");

  uart_puts("CHIRP^ Host: ");
  uart_line(s_host);

  uart_line("CHIRP^ Content-Type: application/octet-stream");
  uart_puts("CHIRP^ Content-Length: ");
  uart_put_uint((unsigned)sizeof body);
  uart_line("");

  if (s_have_cred) {
    uart_puts(s_cred_is_claim ? "CHIRP^ X-Chirp-Claim: " : "CHIRP^ X-Chirp-Token: ");
    uart_line(s_cred);
  }

  if (s_have_key) {
    /* Sign "\ntag=\n" followed by the body. This image has no wall clock, so
     * it sends no nonce, and it sends no tag: both fields are empty. Only the
     * body goes out; Warblet rebuilds the same bytes to check the signature. */
    unsigned char signed_input[CHIRP_SIGN_PREFIX_MAX + sizeof body];
    int           plen = chirp_sign_prefix((char *)signed_input,
                                           CHIRP_SIGN_PREFIX_MAX, 0, 0);
    if (plen < 0) {
      /* Cannot happen with this buffer; say so instead of sending it quietly unsigned. */
      uart_line("CHIRP. signing failed: prefix did not fit -- request NOT signed");
    } else {
      unsigned i;
      for (i = 0u; i < sizeof body; i++) {
        signed_input[(unsigned)plen + i] = body[i];
      }
      chirp_sign(s_key, sizeof s_key, signed_input,
                 (unsigned)plen + (unsigned)sizeof body, sig);
      uart_puts("CHIRP^ X-Chirp-Signature: ");
      uart_line(sig);
    }
  }

  uart_puts("CHIRP^ body ");
  for (unsigned i = 0u; i < sizeof body; i++) { uart_put_hex(body[i], 2); }
  uart_line("");
}

/* ------------------------------------------------------------------ api */

void chirp_send_init(void)
{
  chirp_send_reload();
  if (!s_have_cred) {
    uart_line("CHIRP. uplink off until a credential is stored (USB setup)");
    return;
  }

  uart_puts("CHIRP. uplink ");
  uart_puts(s_host);
  uart_puts(" level=L");
  uart_put_uint(level());
  uart_line(" (cap L2: no TLS stack on this board)");
}

int chirp_send_key_set(void)
{
  return s_have_key;
}

void chirp_send_tick(unsigned now_ms)
{
  if (!s_armed) { s_armed = 1; s_last = now_ms; return; }
  if ((unsigned)(now_ms - s_last) < SEND_PERIOD_MS) { return; }
  s_last = now_ms;
  if (!s_have_cred) {
    /* Never at Open: a request with no credential is not printed. */
    if (!s_told_idle) { uart_line("CHIRP. no uplink: no credential stored yet (USB setup)"); }
    s_told_idle = 1;
    return;
  }
  print_http_request();
}
