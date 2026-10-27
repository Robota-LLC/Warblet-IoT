/* Validate supplied fields, merge with stored settings, then check completeness.
 * These rules depend only on the JSON reader. */
#ifndef CHIRPVAL_H
#define CHIRPVAL_H

/* Per-field rules only; says nothing about missing fields. */
const char *chirp_validate_fields(const char *json, unsigned len);

/* Check field rules and a credential: this image joins no network, so a
 * credential alone is complete. Return 0, bad-json, or missing-field. */
const char *chirp_validate(const char *json, unsigned len);

/* Merge push over stored into out, preserving each value's source text.
 * A NULL stored pointer means no config. Return length, or -1 on invalid input or overflow. */
int chirp_merge(const char *stored, unsigned slen,
                const char *push, unsigned plen,
                char *out, unsigned outsz);

#endif /* CHIRPVAL_H */
