/* Validate supplied fields, merge with stored settings, then check completeness.
 * These rules depend only on the JSON reader. */
#ifndef CHIRPVAL_H
#define CHIRPVAL_H

/* This relay-only image requires a credential but no network.
 * Define CHIRP_REQUIRE_NETWORK=1 for firmware that requires network settings. */
#ifndef CHIRP_REQUIRE_NETWORK
#define CHIRP_REQUIRE_NETWORK 0
#endif

/* Per-field rules only; says nothing about missing fields. */
const char *chirp_validate_fields(const char *json, unsigned len);

/* Check field rules and completeness. Return 0, bad-json, or missing-field. */
const char *chirp_validate(const char *json, unsigned len);

/* Merge push over stored into out, preserving each value's source text.
 * A NULL stored pointer means no config. Return length, or -1 on invalid input or overflow. */
int chirp_merge(const char *stored, unsigned slen,
                const char *push, unsigned plen,
                char *out, unsigned outsz);

#endif /* CHIRPVAL_H */
