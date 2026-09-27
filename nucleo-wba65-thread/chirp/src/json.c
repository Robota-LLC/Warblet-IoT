#include "json.h"

#include <limits.h>

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static const char *skip_ws(const char *p, const char *e)
{
  while (p < e && is_ws(*p)) { p++; }
  return p;
}

static int is_hex(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* p points at the opening quote. Returns the byte after the closing quote, or NULL. */
static const char *skip_string(const char *p, const char *e)
{
  if (p >= e || *p != '"') { return 0; }
  p++;
  while (p < e) {
    unsigned char c = (unsigned char)*p;
    if (c == '"') { return p + 1; }
    if (c == '\\') {
      p++;
      if (p >= e) { return 0; }
      switch (*p) {
        case '"': case '\\': case '/': case 'b': case 'f':
        case 'n': case 'r': case 't':
          p++;
          break;
        case 'u':
          if (e - p < 5) { return 0; }
          for (int i = 1; i <= 4; i++) { if (!is_hex(p[i])) { return 0; } }
          p += 5;
          break;
        default:
          return 0;
      }
      continue;
    }
    if (c < 0x20u) { return 0; }   /* raw control characters are illegal in JSON */
    p++;
  }
  return 0;
}

static const char *skip_value(const char *p, const char *e, int depth);

static const char *skip_number(const char *p, const char *e)
{
  const char *s = p;
  if (p < e && *p == '-') { p++; }
  if (p >= e || *p < '0' || *p > '9') { return 0; }
  if (*p == '0') { p++; }
  else { while (p < e && *p >= '0' && *p <= '9') { p++; } }
  if (p < e && *p == '.') {
    p++;
    if (p >= e || *p < '0' || *p > '9') { return 0; }
    while (p < e && *p >= '0' && *p <= '9') { p++; }
  }
  if (p < e && (*p == 'e' || *p == 'E')) {
    p++;
    if (p < e && (*p == '+' || *p == '-')) { p++; }
    if (p >= e || *p < '0' || *p > '9') { return 0; }
    while (p < e && *p >= '0' && *p <= '9') { p++; }
  }
  return (p > s) ? p : 0;
}

static const char *skip_lit(const char *p, const char *e, const char *lit)
{
  while (*lit) {
    if (p >= e || *p != *lit) { return 0; }
    p++; lit++;
  }
  return p;
}

static const char *skip_object(const char *p, const char *e, int depth)
{
  p++;                                     /* '{' */
  p = skip_ws(p, e);
  if (p < e && *p == '}') { return p + 1; }
  for (;;) {
    p = skip_ws(p, e);
    p = skip_string(p, e);
    if (!p) { return 0; }
    p = skip_ws(p, e);
    if (p >= e || *p != ':') { return 0; }
    p = skip_value(p + 1, e, depth + 1);
    if (!p) { return 0; }
    p = skip_ws(p, e);
    if (p < e && *p == ',') { p++; continue; }
    if (p < e && *p == '}') { return p + 1; }
    return 0;
  }
}

static const char *skip_array(const char *p, const char *e, int depth)
{
  p++;                                     /* '[' */
  p = skip_ws(p, e);
  if (p < e && *p == ']') { return p + 1; }
  for (;;) {
    p = skip_value(p, e, depth + 1);
    if (!p) { return 0; }
    p = skip_ws(p, e);
    if (p < e && *p == ',') { p++; p = skip_ws(p, e); continue; }
    if (p < e && *p == ']') { return p + 1; }
    return 0;
  }
}

static const char *skip_value(const char *p, const char *e, int depth)
{
  if (depth > JS_MAX_DEPTH) { return 0; }
  p = skip_ws(p, e);
  if (p >= e) { return 0; }
  switch (*p) {
    case '"': return skip_string(p, e);
    case '{': return skip_object(p, e, depth);
    case '[': return skip_array(p, e, depth);
    case 't': return skip_lit(p, e, "true");
    case 'f': return skip_lit(p, e, "false");
    case 'n': return skip_lit(p, e, "null");
    default:  return skip_number(p, e);
  }
}

int js_validate(const char *buf, unsigned len)
{
  const char *e = buf + len;
  const char *p = skip_value(buf, e, 0);
  if (!p) { return 0; }
  p = skip_ws(p, e);
  return (p == e);
}

static js_type type_of(char c)
{
  switch (c) {
    case '"': return JS_STR;
    case '{': return JS_OBJ;
    case '[': return JS_ARR;
    case 't': return JS_TRUE;
    case 'f': return JS_FALSE;
    case 'n': return JS_NULL;
    default:  return JS_NUM;
  }
}

static int key_eq(const char *ks, unsigned kn, const char *key)
{
  unsigned i = 0;
  while (i < kn && key[i] && ks[i] == key[i]) { i++; }
  return (i == kn && key[i] == '\0');
}

static void set_val(js_val *out, const char *vs, const char *ve)
{
  out->t = type_of(*vs);
  if (out->t == JS_STR) { out->p = vs + 1; out->n = (unsigned)((ve - 1) - (vs + 1)); }
  else                  { out->p = vs;     out->n = (unsigned)(ve - vs); }
}

int js_obj_next(const char *obj, unsigned len, unsigned *pos,
                js_val *key, js_val *val)
{
  const char *e = obj + len;
  const char *p;

  key->t = JS_NONE; key->p = 0; key->n = 0;
  val->t = JS_NONE; val->p = 0; val->n = 0;

  if (*pos == 0u) {
    /* First call: step over '{'. An empty object ends immediately. */
    p = skip_ws(obj, e);
    if (p >= e || *p != '{') { return -1; }
    p = skip_ws(p + 1, e);
    if (p < e && *p == '}') { *pos = len; return 0; }
  } else {
    if (*pos >= len) { return 0; }
    p = skip_ws(obj + *pos, e);
    if (p < e && *p == '}') { *pos = len; return 0; }
    if (p >= e || *p != ',') { return -1; }
    p = skip_ws(p + 1, e);
  }

  if (p >= e || *p != '"') { return -1; }
  {
    const char *ks = p + 1;
    const char *ke = skip_string(p, e);
    const char *vs, *ve;

    if (!ke) { return -1; }
    key->t = JS_STR;
    key->p = ks;
    key->n = (unsigned)((ke - 1) - ks);

    p = skip_ws(ke, e);
    if (p >= e || *p != ':') { return -1; }
    vs = skip_ws(p + 1, e);
    ve = skip_value(vs, e, 1);
    if (!ve) { return -1; }
    set_val(val, vs, ve);

    *pos = (unsigned)(ve - obj);
  }
  return 1;
}

const char *js_raw(const js_val *v, unsigned *len)
{
  *len = (v->t == JS_STR) ? v->n + 2u : v->n;
  return (v->t == JS_STR) ? v->p - 1  : v->p;
}

int js_obj_get(const char *obj, unsigned len, const char *key, js_val *out)
{
  unsigned pos = 0u;
  js_val   k, v;
  int      r;

  out->t = JS_NONE; out->p = 0; out->n = 0;

  while ((r = js_obj_next(obj, len, &pos, &k, &v)) == 1) {
    if (key_eq(k.p, k.n, key)) { *out = v; return 1; }
  }
  return r;                                /* 0 = absent, -1 = malformed */
}

static int hex4(const char *p)
{
  int v = 0;
  for (int i = 0; i < 4; i++) {
    char c = p[i];
    int d;
    if (c >= '0' && c <= '9') { d = c - '0'; }
    else if (c >= 'a' && c <= 'f') { d = c - 'a' + 10; }
    else if (c >= 'A' && c <= 'F') { d = c - 'A' + 10; }
    else { return -1; }
    v = (v << 4) | d;
  }
  return v;
}

int js_str_copy(const js_val *v, char *dst, unsigned dstsz)
{
  if (v->t != JS_STR || dstsz == 0u) { return -1; }
  unsigned o = 0;
  for (unsigned i = 0; i < v->n; ) {
    char c = v->p[i];
    if (c != '\\') {
      if (o + 1u >= dstsz) { return -1; }
      dst[o++] = c;
      i++;
      continue;
    }
    i++;
    if (i >= v->n) { return -1; }
    char esc = v->p[i++];
    char out;
    switch (esc) {
      case '"': out = '"';  break;
      case '\\': out = '\\'; break;
      case '/': out = '/';  break;
      case 'b': out = '\b'; break;
      case 'f': out = '\f'; break;
      case 'n': out = '\n'; break;
      case 'r': out = '\r'; break;
      case 't': out = '\t'; break;
      case 'u': {
        if (i + 4u > v->n) { return -1; }
        int cp = hex4(v->p + i);
        if (cp < 0) { return -1; }
        i += 4u;
        /* BMP only, and surrogates are rejected rather than silently mangled. */
        if (cp >= 0xD800 && cp <= 0xDFFF) { return -1; }
        if (cp < 0x80) {
          if (o + 1u >= dstsz) { return -1; }
          dst[o++] = (char)cp;
        } else if (cp < 0x800) {
          if (o + 2u >= dstsz) { return -1; }
          dst[o++] = (char)(0xC0 | (cp >> 6));
          dst[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
          if (o + 3u >= dstsz) { return -1; }
          dst[o++] = (char)(0xE0 | (cp >> 12));
          dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
          dst[o++] = (char)(0x80 | (cp & 0x3F));
        }
        continue;
      }
      default: return -1;
    }
    if (o + 1u >= dstsz) { return -1; }
    dst[o++] = out;
  }
  dst[o] = '\0';
  return (int)o;
}

int js_num_int(const js_val *v, long *out)
{
  if (v->t != JS_NUM || v->n == 0u) { return -1; }
  unsigned i = 0;
  int neg = 0;
  if (v->p[0] == '-') { neg = 1; i = 1; }
  if (i >= v->n) { return -1; }
  long acc = 0;
  for (; i < v->n; i++) {
    char c = v->p[i];
    if (c < '0' || c > '9') { return -1; }   /* no floats, no exponents */
    if (acc > (LONG_MAX - (c - '0')) / 10) { return -1; }   /* would overflow */
    acc = acc * 10 + (c - '0');
  }
  *out = neg ? -acc : acc;
  return 0;
}
