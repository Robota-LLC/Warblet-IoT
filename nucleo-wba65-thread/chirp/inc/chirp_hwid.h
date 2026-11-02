/* The id this board answers to: the `hwid` the portal pushed, else the die
 * default `wba65-thread-<low 24 bits of UID word 0>`. One copy, read by the
 * announce and by every report, so the two cannot disagree. */
#ifndef CHIRP_HWID_H
#define CHIRP_HWID_H

#include "chirp_profile.h"

/* Room for the longest id this board holds and its NUL. */
#define CHIRP_HWID_MAX (CHIRP_HWID_LEN_MAX + 1u)

/* CHIRP-PROV v1.8: the announce is at most 512 bytes, before its CRLF. */
#define CHIRP_ANNOUNCE_MAX 512u

/* Set the die default: the id until a config names one. */
void chirp_hwid_setup(unsigned uid24);

/* Take the id from merged, validated config JSON: its `hwid`, or the die
 * default when it has none. */
void chirp_hwid_config(const char *json, unsigned len);

/* The current id; "" before setup. */
const char *chirp_hwid(void);

/* The CHIRP-PROV announce line for this id, CRLF included; key_set says a
 * signing key is held. Returns the byte count written, or 0 when `out` cannot
 * hold the whole line or the line would be over CHIRP_ANNOUNCE_MAX. */
unsigned chirp_announce_line(char *out, unsigned outsz, int key_set);

#endif /* CHIRP_HWID_H */
