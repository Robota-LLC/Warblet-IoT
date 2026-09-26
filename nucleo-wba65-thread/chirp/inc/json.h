/* json.h — a bounded, hand-rolled, allocation-free JSON reader.
 * Strict: no trailing commas, no comments, no NaN/Inf, depth-limited.
 * It never copies until you ask it to, and every copy is length-checked. */
#ifndef JSON_H
#define JSON_H

typedef enum {
  JS_NONE = 0, JS_STR, JS_NUM, JS_OBJ, JS_ARR, JS_TRUE, JS_FALSE, JS_NULL
} js_type;

typedef struct {
  const char *p;   /* for JS_STR: first byte INSIDE the quotes */
  unsigned    n;   /* for JS_STR: length inside the quotes (still escaped) */
  js_type     t;
} js_val;

#define JS_MAX_DEPTH 8

/* 1 when [buf,len) is exactly one well-formed JSON value (plus whitespace). */
int js_validate(const char *buf, unsigned len);

/* obj must point at '{'. 1 = key found, 0 = absent, -1 = malformed. */
int js_obj_get(const char *obj, unsigned len, const char *key, js_val *out);

/* Walk object members in source order with pos starting at zero.
 * Return 1 for a member, 0 at the end, or -1 on malformed input.
 * Returned spans reference the original JSON text. */
int js_obj_next(const char *obj, unsigned len, unsigned *pos,
                js_val *key, js_val *val);

/* A value's source text, quotes included for a JS_STR. */
const char *js_raw(const js_val *v, unsigned *len);

/* Unescape a JS_STR into dst. Returns length, or -1 on overflow/bad escape. */
int js_str_copy(const js_val *v, char *dst, unsigned dstsz);

/* Parse a JS_NUM as an integer. 0 = ok, -1 = not an integer / out of range. */
int js_num_int(const js_val *v, long *out);

#endif /* JSON_H */
