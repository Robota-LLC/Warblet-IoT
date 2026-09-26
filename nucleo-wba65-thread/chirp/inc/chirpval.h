/* Validate supplied fields, merge settings, then check completeness.
 * These functions depend only on the JSON reader. */
#ifndef CHIRPVAL_H
#define CHIRPVAL_H

/* Validate field types, lengths, ranges, and at most one credential.
 * Return 0 or bad-json; completeness is checked after merging. */
const char *chirp_validate_fields(const char *json, unsigned len);

/* Check the merged config for valid fields, a credential, and network settings.
 * Return 0, bad-json, or missing-field. */
const char *chirp_validate(const char *json, unsigned len);

/* Merge push over cur into out, preserving value text. Use {} for an empty config.
 * Validate inputs first. Return the merged length, or 0 if it does not fit. */
unsigned chirp_merge(char *out, unsigned outsz,
                     const char *cur, unsigned curlen,
                     const char *push, unsigned pushlen);

#endif /* CHIRPVAL_H */
