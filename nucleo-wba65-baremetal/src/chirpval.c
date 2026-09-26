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

/* Standard and URL-safe alphabets: the portal's paste field accepts either. */
static int is_b64_char(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') ||
         c == '+' || c == '/' || c == '-' || c == '_';
}

/* A 32-byte signing key is encoded as 44 base64 characters ending in '='. */
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

  if (!have_name || !have_key) { return "missing-field"; }
  return 0;
}

/* Per-field rules, plus what was present, so one pass serves both the push
 * check and the merged check. */
static const char *check_fields(const char *json, unsigned len,
                                int *have_cred, int *have_net)
{
  js_val claim, token, ssid, thread, key;

  *have_cred = 0;
  *have_net  = 0;

  if (len == 0u || !js_validate(json, len) || json[0] != '{') { return "bad-json"; }

  int c = js_obj_get(json, len, "claim", &claim);
  int t = js_obj_get(json, len, "token", &token);
  if (c < 0 || t < 0) { return "bad-json"; }
  if (c == 1 && t == 1) { return "bad-json"; }        /* Require exactly one credential. */
  if (c == 1 && claim.t != JS_STR) { return "bad-json"; }
  if (t == 1 && token.t != JS_STR) { return "bad-json"; }
  *have_cred = (c == 1 || t == 1);

  if (js_obj_get(json, len, "key", &key) == 1) {
    if (key.t != JS_STR || !is_key_b64_32(key.p, key.n)) { return "bad-json"; }
  }

  int has_ssid = js_obj_get(json, len, "ssid", &ssid) == 1;
  if (has_ssid && ssid.t != JS_STR) { return "bad-json"; }

  int has_thread = js_obj_get(json, len, "thread", &thread) == 1;
  if (has_thread && thread.t != JS_OBJ) { return "bad-json"; }
  *have_net = (has_ssid || has_thread);

  if (has_thread) { return check_thread(&thread); }
  return 0;
}

const char *chirp_validate_fields(const char *json, unsigned len)
{
  int cred = 0, net = 0;
  return check_fields(json, len, &cred, &net);
}

const char *chirp_validate(const char *json, unsigned len)
{
  int cred = 0, net = 0;
  const char *err = check_fields(json, len, &cred, &net);

  if (err) { return err; }

  /* A credential is always required. */
  if (!cred) { return "missing-field"; }

  /* This image joins no network, so a credential alone is complete. */
#if CHIRP_REQUIRE_NETWORK
  if (!net) { return "missing-field"; }
#else
  (void)net;
#endif

  return 0;
}

/* ------------------------------------------------------------------ merge */

/* Where a field's value is coming from: the push, or the stored config. */
typedef struct { const char *p; unsigned n; } jsrc;

typedef struct {
  char    *out;
  unsigned cap;        /* usable bytes, one already reserved for the NUL */
  unsigned len;
  int      bad;
} emitter;

static unsigned name_len(const char *s)
{
  unsigned n = 0u;
  while (s[n] != '\0') { n++; }
  return n;
}

static int src_has(jsrc s, const char *name)
{
  js_val v;
  return s.p != 0 && js_obj_get(s.p, s.n, name, &v) == 1;
}

/* Push wins when it carries the field at all; otherwise inherit. */
static jsrc pick(jsrc push, jsrc stored, const char *name)
{
  return src_has(push, name) ? push : stored;
}

static void put(emitter *e, const char *s, unsigned n)
{
  if (e->bad || e->len + n > e->cap) { e->bad = 1; return; }
  for (unsigned i = 0; i < n; i++) { e->out[e->len++] = s[i]; }
}

/* Splice the value's source bytes verbatim, so escapes survive and nothing
 * here re-escapes. */
static void put_field(emitter *e, const char *name, jsrc src)
{
  js_val      v;
  const char *vp;
  unsigned    vn;

  if (src.p == 0 || js_obj_get(src.p, src.n, name, &v) != 1) { return; }

  /* A string's js_val excludes its quotes; widen by one on each side. */
  if (v.t == JS_STR) { vp = v.p - 1; vn = v.n + 2u; }
  else               { vp = v.p;     vn = v.n; }

  if (e->len > 1u) { put(e, ",", 1u); }        /* len == 1 is just the '{' */
  put(e, "\"", 1u);
  put(e, name, name_len(name));
  put(e, "\":", 2u);
  put(e, vp, vn);
}

int chirp_merge(const char *stored, unsigned slen,
                const char *push, unsigned plen,
                char *out, unsigned outsz)
{
  emitter e;
  jsrc    P, S, cred, wifi;

  if (push == 0 || plen == 0u || out == 0 || outsz < 3u) { return -1; }
  if (stored != 0 && slen == 0u) { stored = 0; }

  P.p = push;   P.n = plen;
  S.p = stored; S.n = slen;

  e.out = out;
  e.cap = outsz - 1u;                          /* reserve the NUL */
  e.len = 0u;
  e.bad = 0;

  put(&e, "{", 1u);

  /* A pushed claim or token replaces the stored credential; both together would be invalid. */
  cred = (src_has(P, "claim") || src_has(P, "token")) ? P : S;
  put_field(&e, "claim", cred);
  put_field(&e, "token", cred);

  /* Replace stored Wi-Fi settings as a group when a network is supplied. */
  wifi = (src_has(P, "ssid") || src_has(P, "aps") || src_has(P, "pass")) ? P : S;
  put_field(&e, "ssid", wifi);
  put_field(&e, "pass", wifi);
  put_field(&e, "aps",  wifi);

  /* The rest are independent fields, and the push wins on each one it carries. */
  put_field(&e, "thread", pick(P, S, "thread"));
  put_field(&e, "key",    pick(P, S, "key"));
  put_field(&e, "hwid",   pick(P, S, "hwid"));
  put_field(&e, "host",   pick(P, S, "host"));
  put_field(&e, "level",  pick(P, S, "level"));

  put(&e, "}", 1u);
  if (e.bad) { return -1; }

  out[e.len] = '\0';
  return (int)e.len;
}
