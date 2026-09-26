/* Validate config fields, merge settings, and check completeness.
 * Malformed values return bad-json; missing required fields return missing-field. */

#include "chirpval.h"
#include "json.h"

static int is_hex_str(const char *s, unsigned n)
{
  for (unsigned i = 0; i < n; i++) {
    char c = s[i];
    int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!ok) { return 0; }
  }
  return n > 0u;
}

static int is_b64_char(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '+' || c == '/';
}

/* Require a 32-byte signing key encoded as 44 base64 characters ending in '='. */
static int is_key_b64_32(const char *s, unsigned n)
{
  if (n != 44u || s[43] != '=') { return 0; }
  for (unsigned i = 0; i < 43u; i++) { if (!is_b64_char(s[i])) { return 0; } }
  return 1;
}

static const char *check_thread(const js_val *t)
{
  js_val v;

  if (js_obj_get(t->p, t->n, "dataset", &v) == 1) {
    if (v.t != JS_STR) { return "bad-json"; }
    /* even-length hex, <= 254 bytes of Active Operational Dataset TLVs */
    if (v.n == 0u || (v.n & 1u) || v.n > 508u || !is_hex_str(v.p, v.n)) { return "bad-json"; }
    return 0;                       /* dataset wins: nothing else in here is read */
  }

  int have_name = 0, have_key = 0;

  if (js_obj_get(t->p, t->n, "name", &v) == 1) {
    if (v.t != JS_STR || v.n == 0u || v.n > 16u) { return "bad-json"; }
    have_name = 1;
  }
  if (js_obj_get(t->p, t->n, "key", &v) == 1) {
    if (v.t != JS_STR || v.n != 32u || !is_hex_str(v.p, v.n)) { return "bad-json"; }
    have_key = 1;
  }
  if (js_obj_get(t->p, t->n, "panid", &v) == 1) {
    if (v.t != JS_STR || v.n != 4u || !is_hex_str(v.p, v.n)) { return "bad-json"; }
  }
  if (js_obj_get(t->p, t->n, "xpanid", &v) == 1) {
    if (v.t != JS_STR || v.n != 16u || !is_hex_str(v.p, v.n)) { return "bad-json"; }
  }
  if (js_obj_get(t->p, t->n, "channel", &v) == 1) {
    long ch = 0;
    if (v.t != JS_NUM || js_num_int(&v, &ch) != 0 || ch < 11 || ch > 26) { return "bad-json"; }
  }

  /* This stays a FIELD rule, not a completeness rule: `thread` is replaced
   * whole and never merged member by member, so a half-written one is a
   * mistake in this push that no stored record can repair. */
  if (!have_name || !have_key) { return "missing-field"; }
  return 0;
}

const char *chirp_validate_fields(const char *json, unsigned len)
{
  js_val claim, token, ssid, thread, key;

  if (len == 0u || !js_validate(json, len) || json[0] != '{') { return "bad-json"; }

  int c = js_obj_get(json, len, "claim", &claim);
  int t = js_obj_get(json, len, "token", &token);
  if (c < 0 || t < 0) { return "bad-json"; }
  if (c == 1 && t == 1) { return "bad-json"; }        /* Allow at most one credential. */
  if (c == 1 && claim.t != JS_STR) { return "bad-json"; }
  if (t == 1 && token.t != JS_STR) { return "bad-json"; }

  if (js_obj_get(json, len, "key", &key) == 1) {
    if (key.t != JS_STR || !is_key_b64_32(key.p, key.n)) { return "bad-json"; }
  }

  if (js_obj_get(json, len, "ssid", &ssid) == 1 && ssid.t != JS_STR) { return "bad-json"; }

  if (js_obj_get(json, len, "thread", &thread) == 1) {
    if (thread.t != JS_OBJ) { return "bad-json"; }
    return check_thread(&thread);
  }
  return 0;
}

const char *chirp_validate(const char *json, unsigned len)
{
  const char *err = chirp_validate_fields(json, len);
  js_val      v;

  if (err) { return err; }

  /* Check completeness on the merged config. */
  if (js_obj_get(json, len, "claim", &v) != 1 &&
      js_obj_get(json, len, "token", &v) != 1) { return "missing-field"; }

  /* This board has no Wi-Fi. A config with neither Wi-Fi credentials nor a
   * thread object carries nothing any device could act on. */
  if (js_obj_get(json, len, "ssid", &v) != 1 &&
      js_obj_get(json, len, "thread", &v) != 1) { return "missing-field"; }

  return 0;
}

/* ------------------------------------------------------------------ merge */

static int key_is(const js_val *k, const char *name)
{
  unsigned i = 0u;
  while (i < k->n && name[i] != '\0' && k->p[i] == name[i]) { i++; }
  return (i == k->n && name[i] == '\0');
}

static int has(const char *obj, unsigned len, const char *name)
{
  js_val v;
  return js_obj_get(obj, len, name, &v) == 1;
}

/* Append `"<key>":<value>`, with a leading comma when this is not the first
 * member. Both halves are copied as their original source text — which is what
 * makes this a splicer rather than a JSON writer: no value is re-escaped,
 * re-formatted or reinterpreted on its way through. 0 = would not fit. */
static int emit(char *out, unsigned outsz, unsigned *o, int *first,
                const js_val *k, const js_val *v)
{
  unsigned    vlen;
  const char *vraw = js_raw(v, &vlen);
  unsigned    need = (*first ? 0u : 1u) + 1u + k->n + 1u + 1u + vlen;
  unsigned    i;

  if (*o + need > outsz) { return 0; }

  if (!*first) { out[(*o)++] = ','; }
  *first = 0;

  out[(*o)++] = '"';
  for (i = 0u; i < k->n; i++) { out[(*o)++] = k->p[i]; }
  out[(*o)++] = '"';
  out[(*o)++] = ':';
  for (i = 0u; i < vlen; i++) { out[(*o)++] = vraw[i]; }
  return 1;
}

unsigned chirp_merge(char *out, unsigned outsz,
                     const char *cur, unsigned curlen,
                     const char *push, unsigned pushlen)
{
  unsigned o = 0u, pos;
  int      first = 1;
  js_val   k, v;

  /* Replace credentials and Wi-Fi networks as groups.
   * A supplied claim or token replaces either stored credential;
   * a single ssid without aps replaces the stored network list. */
  int push_cred = has(push, pushlen, "claim") || has(push, pushlen, "token");
  int drop_aps  = has(push, pushlen, "ssid") && !has(push, pushlen, "aps");

  if (outsz < 2u) { return 0u; }
  out[o++] = '{';

  /* The push first, in its own order, so what changed reads first. */
  pos = 0u;
  while (js_obj_next(push, pushlen, &pos, &k, &v) == 1) {
    if (!emit(out, outsz, &o, &first, &k, &v)) { return 0u; }
  }

  /* Then everything stored that the push did not speak to. */
  pos = 0u;
  while (js_obj_next(cur, curlen, &pos, &k, &v) == 1) {
    char     keyz[32];
    unsigned i;

    /* Preserve keys too long to match the recognized field names. */
    if (k.n < sizeof keyz) {
      for (i = 0u; i < k.n; i++) { keyz[i] = k.p[i]; }
      keyz[k.n] = '\0';
      if (has(push, pushlen, keyz)) { continue; }
    }

    if (push_cred && (key_is(&k, "claim") || key_is(&k, "token"))) { continue; }
    if (drop_aps  && key_is(&k, "aps"))                            { continue; }

    if (!emit(out, outsz, &o, &first, &k, &v)) { return 0u; }
  }

  if (o + 1u > outsz) { return 0u; }
  out[o++] = '}';
  return o;
}
