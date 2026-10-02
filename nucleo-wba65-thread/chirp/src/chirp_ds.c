/* chirp_ds.c — see chirp_ds.h. No hardware, no OpenThread, no allocation. */

#include "chirp_ds.h"
#include "json.h"

static int hexval(char c)
{
  if (c >= '0' && c <= '9') { return c - '0'; }
  if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
  if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
  return -1;
}

int chirp_hex2bin(const char *hex, unsigned n, unsigned char *out, unsigned outsz)
{
  if (n == 0u || (n & 1u)) { return -1; }
  if (n / 2u > outsz)      { return -1; }

  for (unsigned i = 0; i < n; i += 2u) {
    int hi = hexval(hex[i]);
    int lo = hexval(hex[i + 1u]);
    if (hi < 0 || lo < 0) { return -1; }
    out[i / 2u] = (unsigned char)((hi << 4) | lo);
  }
  return (int)(n / 2u);
}

/* Zero the struct without <string.h>. */
static void ds_clear(chirp_ds_t *d)
{
  unsigned char *p = (unsigned char *)d;
  for (unsigned i = 0; i < sizeof *d; i++) { p[i] = 0u; }
}

int chirp_ds_parse(const char *json, unsigned len, chirp_ds_t *out)
{
  js_val t, v;
  int    n;

  ds_clear(out);

  if (js_obj_get(json, len, "thread", &t) != 1 || t.t != JS_OBJ) { return 0; }

  /* The dataset wins over discrete fields, the same precedence chirpval.c enforces. */
  if (js_obj_get(t.p, t.n, "dataset", &v) == 1 && v.t == JS_STR) {
    n = chirp_hex2bin(v.p, v.n, out->tlv, CHIRP_DS_TLV_MAX);
    if (n <= 0) { return -1; }
    out->tlv_len  = (unsigned)n;
    out->have_tlv = 1;
    return 1;
  }

  if (js_obj_get(t.p, t.n, "key", &v) == 1 && v.t == JS_STR) {
    if (chirp_hex2bin(v.p, v.n, out->key, sizeof out->key) != 16) { return -1; }
    out->have_key = 1;
  }

  if (js_obj_get(t.p, t.n, "name", &v) == 1 && v.t == JS_STR) {
    int nl = js_str_copy(&v, out->name, sizeof out->name);
    if (nl < 0) { return -1; }
    out->have_name = 1;
  }

  if (js_obj_get(t.p, t.n, "panid", &v) == 1 && v.t == JS_STR) {
    unsigned char b[2];
    if (chirp_hex2bin(v.p, v.n, b, sizeof b) != 2) { return -1; }
    out->panid      = ((unsigned)b[0] << 8) | (unsigned)b[1];
    out->have_panid = 1;
  }

  if (js_obj_get(t.p, t.n, "xpanid", &v) == 1 && v.t == JS_STR) {
    if (chirp_hex2bin(v.p, v.n, out->xpanid, sizeof out->xpanid) != 8) { return -1; }
    out->have_xpanid = 1;
  }

  if (js_obj_get(t.p, t.n, "channel", &v) == 1 && v.t == JS_NUM) {
    long ch = 0;
    if (js_num_int(&v, &ch) != 0 || ch < 11 || ch > 26) { return -1; }
    out->channel      = (unsigned)ch;
    out->have_channel = 1;
  }

  return 1;
}
