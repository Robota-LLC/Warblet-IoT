/* Check one setup push (CHIRP-PROV v1.8) and merge it over the stored
 * settings into the record the board stores. The same file in both Nucleo
 * demos; chirp_profile.h says what this board takes. */
#ifndef CHIRPVAL_H
#define CHIRPVAL_H

#include "chirp_profile.h"

/* The contract's bounds, the same on every board. */
#define CHIRP_CRED_MAX    63u    /* claim or token: 1-63 printable ASCII */
#define CHIRP_HOST_MAX    63u    /* letters, digits, '.' and '-' */
#define CHIRP_SSID_MAX    32u    /* bytes */
#define CHIRP_PASS_MAX    64u    /* bytes */
#define CHIRP_APS_MAX     8u     /* networks in aps */
#define CHIRP_KEY_LEN     44u    /* a signing key: standard base64 of 32 bytes */
#define CHIRP_DATASET_MAX 254u   /* Thread dataset bytes: 508 hex digits */
#define CHIRP_TNAME_MAX   16u    /* Thread network name bytes */
#define CHIRP_FIELD_MAX   32u    /* a field name a refusal can carry */

/* "CHIRP= err unsupported <field>", the longest answer, fits this. */
#define CHIRP_REPLY_MAX   64u

/* The longest record chirp_merge() writes: every value at its bound, and every
 * byte of text one that needs an escape (" or \). The store holds this much. */
#define CHIRP_TEXT(n)       (2u + 2u * (n))       /* a string that may need escapes */
#define CHIRP_PLAIN(n)      (2u + (n))            /* hex, an id, a host, a key */
#define CHIRP_MEMBER(nm, v) (sizeof(nm) + 3u + (v))   /* ,"nm": and its value */
#if CHIRP_TAKES_WIFI
#define CHIRP_NETWORK_MAX   (sizeof "{\"ssid\":,\"pass\":}" - 1u + \
                             CHIRP_TEXT(CHIRP_SSID_MAX) + CHIRP_TEXT(CHIRP_PASS_MAX))
#define CHIRP_WIFI_MAX      (CHIRP_MEMBER("ssid", CHIRP_TEXT(CHIRP_SSID_MAX)) + \
                             CHIRP_MEMBER("pass", CHIRP_TEXT(CHIRP_PASS_MAX)) + \
                             CHIRP_MEMBER("aps", 2u + CHIRP_APS_MAX * (CHIRP_NETWORK_MAX + 1u)))
#else
#define CHIRP_WIFI_MAX      0u
#endif
#define CHIRP_RECORD_MAX    (2u + CHIRP_WIFI_MAX + \
                             CHIRP_MEMBER("thread", sizeof "{\"dataset\":}" - 1u + \
                                                    CHIRP_PLAIN(2u * CHIRP_DATASET_MAX)) + \
                             CHIRP_MEMBER("claim", CHIRP_TEXT(CHIRP_CRED_MAX)) + \
                             CHIRP_MEMBER("hwid", CHIRP_PLAIN(CHIRP_HWID_LEN_MAX)) + \
                             CHIRP_MEMBER("host", CHIRP_PLAIN(CHIRP_HOST_MAX)) + \
                             CHIRP_MEMBER("key", CHIRP_PLAIN(CHIRP_KEY_LEN)))

/* Check one push, the JSON after "CHIRP+ ", and merge it over the stored
 * record (stored is NULL when nothing is stored). cur_id is the id the board
 * answers to now: its stored hwid, or its own. Returns 1 when out holds the
 * new record, complete and NUL-terminated, with *outlen its length, ready to
 * store. Returns 0 for a refusal, with the line to answer in reply
 * ("CHIRP= err bad-json", "CHIRP= err unsupported level", ...); the stored
 * record is never touched. */
int chirp_merge(const char *stored, unsigned slen, const char *push, unsigned plen,
                const char *cur_id, char *out, unsigned outsz, unsigned *outlen,
                char reply[CHIRP_REPLY_MAX]);

#endif /* CHIRPVAL_H */
