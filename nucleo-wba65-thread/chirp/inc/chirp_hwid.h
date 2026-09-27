/* The id this board answers to: the `hwid` the portal pushed, else the die
 * default `wba65-thread-<low 24 bits of UID word 0>`. One copy, read by the
 * announce and by every report, so the two cannot disagree. */
#ifndef CHIRP_HWID_H
#define CHIRP_HWID_H

#define CHIRP_HWID_MAX 48u

/* Set the die default: the id until a config names one. */
void chirp_hwid_setup(unsigned uid24);

/* Take the `hwid` out of merged, validated config JSON, when it has one. */
void chirp_hwid_config(const char *json, unsigned len);

/* The current id; "" before setup. */
const char *chirp_hwid(void);

/* The CHIRP-PROV announce line for this id, CRLF included. Returns the byte
 * count written, or 0 when `out` cannot hold the whole line. */
unsigned chirp_announce_line(char *out, unsigned outsz);

#endif /* CHIRP_HWID_H */
