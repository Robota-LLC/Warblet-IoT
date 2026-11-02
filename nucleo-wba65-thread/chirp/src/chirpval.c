/* Check one setup push and merge it over the stored settings (CHIRP-PROV
 * v1.8). The same file in both Nucleo demos: chirp_profile.h says what this
 * board takes. A push is taken whole or refused whole, and a refusal comes in
 * the contract's order: bad-json for anything but one JSON object, then
 * unsupported <field>, then bad-json for a bad value, then missing-field. */

#include "chirpval.h"
#include "json.h"

static const char BAD[]     = "bad-json";
static const char MISSING[] = "missing-field";

/* The fields this board acts on. Any other is refused by name. */
static const char *const TAKES[] = {
#if CHIRP_TAKES_WIFI
  "ssid", "pass", "aps",
#endif
  "thread", "hwid", "host", "claim", "token", "key"
};

#define TAKES_N (sizeof TAKES / sizeof TAKES[0])

/* Every setting this board keeps, decoded. Text is NUL-terminated; "" is unset. */
typedef struct {
#if CHIRP_TAKES_WIFI
  char     ssid[CHIRP_SSID_MAX + 1u];
  char     pass[CHIRP_PASS_MAX + 1u];
  unsigned ap_count;
  struct {
    char ssid[CHIRP_SSID_MAX + 1u];
    char pass[CHIRP_PASS_MAX + 1u];
  } aps[CHIRP_APS_MAX];
#endif
  int      thread;                                /* 0 none, 1 a dataset, 2 a name and key */
  char     dataset[2u * CHIRP_DATASET_MAX + 1u];  /* hex */
  char     tname[CHIRP_TNAME_MAX + 1u];
  char     tkey[33];                              /* the network key, 32 hex */
  char     panid[5];                              /* 4 hex, or "" */
  char     xpanid[17];                            /* 16 hex, or "" */
  long     channel;                               /* 11 to 26, or 0 */
  char     cred[CHIRP_CRED_MAX + 1u];             /* the claim code or the token */
  int      claim;                                 /* cred is a claim code */
  char     hwid[CHIRP_HWID_LEN_MAX + 1u];         /* "": the board's own id */
  char     host[CHIRP_HOST_MAX + 1u];             /* "": Warblet's own */
  char     key[CHIRP_KEY_LEN + 1u];               /* base64 */
} settings;

/* ------------------------------------------------------------------ small text */

static unsigned text_len(const char *s)
{
  unsigned n = 0u;
  while (s[n] != '\0') { n++; }
  return n;
}

static int text_eq(const char *a, const char *b)
{
  while (*a != '\0' && *a == *b) { a++; b++; }
  return *a == *b;
}

/* dst is at least as big as src's field. */
static void text_copy(char *dst, const char *src)
{
  while ((*dst++ = *src++) != '\0') { }
}

/* a before b, byte by byte, a prefix first. */
static int sorts_before(const char *a, const char *b)
{
  while (*a != '\0' && *a == *b) { a++; b++; }
  return (unsigned char)*a < (unsigned char)*b;
}

static int is_hex(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int is_alnum(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* The platform's id grammar: letters, digits and : - _ . only, so an id cannot
 * put a space or a line break into the announce, a request or a URL. */
static int is_id(char c)   { return is_alnum(c) || c == ':' || c == '-' || c == '_' || c == '.'; }

/* A host is a bare name: letters, digits, dots and dashes. */
static int is_host(char c) { return is_alnum(c) || c == '.' || c == '-'; }

/* Standard base64, the only alphabet a key is written in. */
static int is_b64(char c)  { return is_alnum(c) || c == '+' || c == '/'; }

/* A field name a refusal can carry on the line. */
static int is_name(char c) { return is_alnum(c) || c == '_' || c == '.' || c == '-'; }

/* A claim code or token is printable ASCII without the space, so as many
 * bytes as characters, and never two fields of a space-separated frame. */
static int is_printable(char c) { return c >= 0x21 && c <= 0x7E; }

static int all(const char *s, int (*ok)(char))
{
  for (; *s != '\0'; s++) { if (!ok(*s)) { return 0; } }
  return 1;
}

/* ------------------------------------------------------------------ one value */

/* A string value into dst: it fits, and holds no character below U+0020 and
 * no U+007F (these values reach the console, a request and the record). A
 * network's name and password are held to it too (CHIRP-PROV v1.8), which
 * keeps every stored byte at two record bytes or fewer: put_text() writes a
 * byte at or above 0x80 as it is. 1 = taken; 0 leaves dst empty. */
static int take_text(const js_val *v, char *dst, unsigned dstsz)
{
  int n, i = 0;

  if (v->t == JS_STR && (n = js_str_copy(v, dst, dstsz)) >= 0) {
    while (i < n && (unsigned char)dst[i] >= 0x20u && dst[i] != 0x7F) { i++; }
    if (i == n) { return 1; }
  }
  dst[0] = '\0';
  return 0;
}

/* Hex digits into dst: exactly want of them, or with want 0 an even number. */
static int take_hex(const js_val *v, char *dst, unsigned dstsz, unsigned want)
{
  unsigned n;

  if (take_text(v, dst, dstsz) && all(dst, is_hex) && (n = text_len(dst)) > 0u &&
      (want != 0u ? n == want : (n & 1u) == 0u)) {
    return 1;
  }
  dst[0] = '\0';
  return 0;
}

/* A signing key, written the one way the contract allows: 44 characters of
 * standard base64, the last its '=' pad, which is exactly 32 bytes. Never "". */
static int take_key(const js_val *v, char *dst)
{
  unsigned i = 0u;

  if (take_text(v, dst, CHIRP_KEY_LEN + 1u) && text_len(dst) == CHIRP_KEY_LEN &&
      dst[CHIRP_KEY_LEN - 1u] == '=') {
    while (i + 1u < CHIRP_KEY_LEN && is_b64(dst[i])) { i++; }
    if (i + 1u == CHIRP_KEY_LEN) { return 1; }
  }
  dst[0] = '\0';
  return 0;
}

/* A field's name, decoded: 1 when it is 1 to 32 of A-Z a-z 0-9 _ . - */
static int field_name(const js_val *k, char name[CHIRP_FIELD_MAX + 1u])
{
  int n = js_str_copy(k, name, CHIRP_FIELD_MAX + 1u);
  return n > 0 && (unsigned)n == text_len(name) && all(name, is_name);
}

#if CHIRP_TAKES_WIFI
/* aps: the whole list, 1 to 8 networks, each exactly an ssid (not empty) and a pass. */
static int take_networks(const js_val *list, settings *s)
{
  unsigned pos = 0u, n = 0u;
  js_val   entry, k, v;
  char     name[CHIRP_FIELD_MAX + 1u];
  int      r;

  s->ap_count = 0u;
  if (list->t != JS_ARR) { return 0; }
  while ((r = js_arr_next(list->p, list->n, &pos, &entry)) == 1) {
    unsigned at = 0u;
    int      got_ssid = 0, got_pass = 0;

    if (n == CHIRP_APS_MAX || entry.t != JS_OBJ) { return 0; }
    while ((r = js_obj_next(entry.p, entry.n, &at, &k, &v)) == 1) {
      if (field_name(&k, name) && text_eq(name, "ssid") && !got_ssid) {
        got_ssid = take_text(&v, s->aps[n].ssid, sizeof s->aps[n].ssid) && s->aps[n].ssid[0] != '\0';
        if (!got_ssid) { return 0; }
      } else if (field_name(&k, name) && text_eq(name, "pass") && !got_pass) {
        got_pass = take_text(&v, s->aps[n].pass, sizeof s->aps[n].pass);
        if (!got_pass) { return 0; }
      } else {
        return 0;                          /* anything else, or a name twice */
      }
    }
    if (r < 0 || !got_ssid || !got_pass) { return 0; }
    n++;
  }
  if (r < 0 || n == 0u) { return 0; }
  s->ap_count = n;
  return 1;
}
#endif

/* thread: a dataset (even hex, up to 254 bytes), which wins over everything
 * else in the object and is all that is kept; or a name (1-16 bytes) and a
 * network key (32 hex), with a panid (4 hex), an xpanid (16 hex) and a channel
 * (11-26) when it has them. Returns 0, bad-json, or missing-field for a name
 * or a key left out. */
static const char *take_thread(const js_val *t, settings *s)
{
  js_val v;

  s->thread  = 0;
  s->channel = 0;
  s->dataset[0] = s->tname[0] = s->tkey[0] = s->panid[0] = s->xpanid[0] = '\0';
  if (t->t != JS_OBJ) { return BAD; }

  if (js_obj_get(t->p, t->n, "dataset", &v) == 1) {
    if (!take_hex(&v, s->dataset, sizeof s->dataset, 0u)) { return BAD; }
    s->thread = 1;
    return 0;
  }
  if (js_obj_get(t->p, t->n, "name", &v) == 1 &&
      (!take_text(&v, s->tname, sizeof s->tname) || s->tname[0] == '\0')) { return BAD; }
  if (js_obj_get(t->p, t->n, "key", &v) == 1 &&
      !take_hex(&v, s->tkey, sizeof s->tkey, 32u)) { return BAD; }
  if (js_obj_get(t->p, t->n, "panid", &v) == 1 &&
      !take_hex(&v, s->panid, sizeof s->panid, 4u)) { return BAD; }
  if (js_obj_get(t->p, t->n, "xpanid", &v) == 1 &&
      !take_hex(&v, s->xpanid, sizeof s->xpanid, 16u)) { return BAD; }
  if (js_obj_get(t->p, t->n, "channel", &v) == 1 &&
      (v.t != JS_NUM || js_num_int(&v, &s->channel) != 0 || s->channel < 11 || s->channel > 26)) {
    s->channel = 0;
    return BAD;
  }
  if (s->tname[0] == '\0' || s->tkey[0] == '\0') { return MISSING; }
  s->thread = 2;
  return 0;
}

/* One field this board takes, into s. Returns 0, bad-json (the field is left
 * empty), or missing-field for a thread object without its name or key. */
static const char *take_field(const char *name, const js_val *v, settings *s)
{
  if (text_eq(name, "claim") || text_eq(name, "token")) {
    /* One credential slot: a claim code is kept where the token goes, and the
     * sender is told which of the two it is. */
    s->claim = name[0] == 'c';
    if (take_text(v, s->cred, sizeof s->cred) && s->cred[0] != '\0' && all(s->cred, is_printable)) {
      return 0;
    }
    s->cred[0] = '\0';
    return BAD;
  }
  if (text_eq(name, "hwid")) {
    /* "" is no id, so the result is incomplete, on this board with an id of
     * its own too: the page puts a board back on that id by pushing it. */
    if (take_text(v, s->hwid, sizeof s->hwid) && all(s->hwid, is_id)) { return s->hwid[0] != '\0' ? 0 : MISSING; }
    s->hwid[0] = '\0';
    return BAD;
  }
  if (text_eq(name, "host")) {
    /* "" is Warblet's own host. */
    if (take_text(v, s->host, sizeof s->host) && all(s->host, is_host)) { return 0; }
    s->host[0] = '\0';
    return BAD;
  }
  if (text_eq(name, "key")) { return take_key(v, s->key) ? 0 : BAD; }
  if (text_eq(name, "thread")) { return take_thread(v, s); }
#if CHIRP_TAKES_WIFI
  if (text_eq(name, "ssid")) {
    /* 1 to 32 bytes: an empty one names no network. */
    if (take_text(v, s->ssid, sizeof s->ssid) && s->ssid[0] != '\0') { return 0; }
    s->ssid[0] = '\0';
    return BAD;
  }
  if (text_eq(name, "pass")) { return take_text(v, s->pass, sizeof s->pass) ? 0 : BAD; }
  if (text_eq(name, "aps"))  { return take_networks(v, s) ? 0 : BAD; }
#endif
  return BAD;
}

/* ------------------------------------------------------------------ the push */

/* The first look at a push: one JSON object, naming only fields this board
 * takes. Returns 0, bad-json, or unsupported with the field's name in field:
 * of several, the one whose name sorts first byte by byte, since a parsed
 * object keeps no order on every board and the line's order cannot decide. A
 * name the refusal could not carry on the line is bad-json. */
static const char *check_fields(const char *push, unsigned plen, char field[CHIRP_FIELD_MAX + 1u])
{
  unsigned pos = 0u, i;
  int      r, unnameable = 0;
  js_val   k, v;
  char     name[CHIRP_FIELD_MAX + 1u];

  field[0] = '\0';
  if (plen == 0u || !js_validate(push, plen)) { return BAD; }
  while ((r = js_obj_next(push, plen, &pos, &k, &v)) == 1) {
    if (!field_name(&k, name)) { unnameable = 1; continue; }
    for (i = 0u; i < TAKES_N && !text_eq(name, TAKES[i]); i++) { }
    if (i == TAKES_N && (field[0] == '\0' || sorts_before(name, field))) { text_copy(field, name); }
  }
  if (r < 0 || unnameable) { field[0] = '\0'; return BAD; }
  return field[0] != '\0' ? "unsupported" : 0;
}

/* The stored record into s, read as strictly as a push but never refused: a
 * value that would be refused now is left out, and so is a field this board
 * does not take. */
static void read_stored(const char *json, unsigned len, settings *s)
{
  unsigned       pos = 0u, i;
  unsigned char *p = (unsigned char *)s;
  js_val         k, v;
  char           name[CHIRP_FIELD_MAX + 1u];

  for (i = 0u; i < sizeof *s; i++) { p[i] = 0u; }
  if (json == 0) { return; }
  while (js_obj_next(json, len, &pos, &k, &v) == 1) {
    if (field_name(&k, name)) { (void)take_field(name, &v, s); }
  }
}

/* The push over the stored settings in s. Returns 0 when the result is
 * complete, else bad-json or missing-field. */
static const char *take_push(const char *push, unsigned plen, const char *cur_id, settings *s)
{
  unsigned    pos = 0u;
  js_val      k, v;
  char        name[CHIRP_FIELD_MAX + 1u];
  const char *why, *incomplete = 0;
  int         claim = 0, token = 0, key = 0, new_id = 0;
#if CHIRP_TAKES_WIFI
  int         ssid = 0, pass = 0, aps = 0;
#endif

  while (js_obj_next(push, plen, &pos, &k, &v) == 1) {
    (void)field_name(&k, name);            /* check_fields() has read every name */
    why = take_field(name, &v, s);
    if (why == BAD)     { return BAD; }
    if (why == MISSING) { incomplete = MISSING; }
    claim |= text_eq(name, "claim");
    token |= text_eq(name, "token");
    key   |= text_eq(name, "key");
    /* An id other than the one the board answers to, compared byte for byte
     * as Warblet tells devices apart. An empty one is no id: it starts nothing. */
    new_id |= text_eq(name, "hwid") && s->hwid[0] != '\0' && !text_eq(s->hwid, cur_id);
#if CHIRP_TAKES_WIFI
    ssid |= text_eq(name, "ssid");
    pass |= text_eq(name, "pass");
    aps  |= text_eq(name, "aps");
#endif
  }
  if (claim && token) { return BAD; }      /* one credential slot */

#if CHIRP_TAKES_WIFI
  if (aps) {
    /* aps is the whole list, its first network tried first: an ssid or a pass
     * sent beside it must be that network's. */
    if ((ssid && !text_eq(s->ssid, s->aps[0].ssid)) || (pass && !text_eq(s->pass, s->aps[0].pass))) {
      return BAD;
    }
    text_copy(s->ssid, s->aps[0].ssid);
    text_copy(s->pass, s->aps[0].pass);
  } else if (ssid) {
    s->ap_count = 0u;                      /* an ssid without aps names exactly one network */
  } else if (pass && s->ap_count > 0u) {
    /* A pass alone is the password of the network tried first: it changes in
     * the list too, and the list keeps the rest. */
    text_copy(s->aps[0].pass, s->pass);
  }
#endif

  /* A setup code, or another id, starts a new device: the old device's
   * signing key goes, unless this push brings one. A token, networks or a key
   * alone keep it. */
  if ((claim || new_id) && !key) { s->key[0] = '\0'; }

  if (incomplete != 0) { return incomplete; }
  if (s->cred[0] == '\0' || (CHIRP_NEEDS_THREAD && s->thread == 0)) { return MISSING; }
  return 0;
}

/* ------------------------------------------------------------------ the record */

typedef struct {
  char    *out;
  unsigned room;   /* bytes that may be written; the NUL's byte is kept back */
  unsigned len;
  int      full;
} writer;

static void put(writer *w, const char *s)
{
  for (; *s != '\0'; s++) {
    if (w->len >= w->room) { w->full = 1; return; }
    w->out[w->len++] = *s;
  }
}

/* A string, as JSON: in quotes, a backslash before each " and \, and a
 * control character (only a network's name or password holds one) escaped,
 * so the record reads back as it was. */
static void put_text(writer *w, const char *s)
{
  static const char hex[] = "0123456789abcdef";

  put(w, "\"");
  for (; *s != '\0'; s++) {
    unsigned char b = (unsigned char)*s;
    char          c[7] = { '\\', *s, '\0' };
    if (b < 0x20u) {
      const char *shortcut = b == '\b' ? "b" : b == '\f' ? "f" : b == '\n' ? "n" : b == '\r' ? "r" : b == '\t' ? "t" : 0;
      if (shortcut != 0) {
        c[1] = shortcut[0];
      } else {
        c[1] = 'u'; c[2] = '0'; c[3] = '0'; c[4] = hex[b >> 4]; c[5] = hex[b & 15u]; c[6] = '\0';
      }
      put(w, c);
    } else {
      put(w, (*s == '"' || *s == '\\') ? c : c + 1);
    }
  }
  put(w, "\"");
}

/* ,"name": with the comma only after the first member. */
static void put_name(writer *w, const char *name)
{
  if (w->len > 1u) { put(w, ","); }
  put(w, "\"");
  put(w, name);
  put(w, "\":");
}

static void put_member(writer *w, const char *name, const char *text)
{
  put_name(w, name);
  put_text(w, text);
}

/* The settings as the record the board stores: compact JSON, empty values
 * left out, the fields in the order the setup page reads them. */
static void write_record(writer *w, const settings *s)
{
  put(w, "{");
#if CHIRP_TAKES_WIFI
  if (s->ssid[0] != '\0') { put_member(w, "ssid", s->ssid); }
  if (s->ssid[0] != '\0' || s->pass[0] != '\0') { put_member(w, "pass", s->pass); }
  if (s->ap_count > 0u) {
    unsigned i;
    put_name(w, "aps");
    for (i = 0u; i < s->ap_count; i++) {
      put(w, i == 0u ? "[{\"ssid\":" : ",{\"ssid\":");
      put_text(w, s->aps[i].ssid);
      put(w, ",\"pass\":");
      put_text(w, s->aps[i].pass);
      put(w, "}");
    }
    put(w, "]");
  }
#endif
  if (s->thread == 1) {
    put_name(w, "thread");
    put(w, "{\"dataset\":");
    put_text(w, s->dataset);
    put(w, "}");
  } else if (s->thread == 2) {
    put_name(w, "thread");
    put(w, "{\"name\":");
    put_text(w, s->tname);
    put(w, ",\"key\":");
    put_text(w, s->tkey);
    if (s->panid[0] != '\0')  { put(w, ",\"panid\":");  put_text(w, s->panid); }
    if (s->xpanid[0] != '\0') { put(w, ",\"xpanid\":"); put_text(w, s->xpanid); }
    if (s->channel != 0) {
      char d[3] = { (char)('0' + s->channel / 10), (char)('0' + s->channel % 10), '\0' };
      put(w, ",\"channel\":");
      put(w, d);
    }
    put(w, "}");
  }
  if (s->cred[0] != '\0') { put_member(w, s->claim ? "claim" : "token", s->cred); }
  if (s->hwid[0] != '\0') { put_member(w, "hwid", s->hwid); }
  if (s->host[0] != '\0') { put_member(w, "host", s->host); }
  if (s->key[0] != '\0')  { put_member(w, "key", s->key); }
  put(w, "}");
}

/* ------------------------------------------------------------------ api */

int chirp_merge(const char *stored, unsigned slen, const char *push, unsigned plen,
                const char *cur_id, char *out, unsigned outsz, unsigned *outlen,
                char reply[CHIRP_REPLY_MAX])
{
  static settings s;                       /* too big for a stack */
  char            field[CHIRP_FIELD_MAX + 1u];
  const char     *why = check_fields(push, plen, field);
  writer          w = { out, outsz > 0u ? outsz - 1u : 0u, 0u, outsz == 0u };

  if (why == 0) {
    read_stored(stored, slen, &s);
    why = take_push(push, plen, cur_id, &s);
  }
  if (why == 0) {
    write_record(&w, &s);
    if (w.full) { why = "store-failed"; }  /* out is smaller than CHIRP_RECORD_MAX */
  }
  if (why != 0) {
    writer r = { reply, CHIRP_REPLY_MAX - 1u, 0u, 0 };
    put(&r, "CHIRP= err ");
    put(&r, why);
    if (field[0] != '\0') { put(&r, " "); put(&r, field); }
    reply[r.len] = '\0';
    return 0;
  }
  out[w.len] = '\0';
  *outlen = w.len;
  return 1;
}
