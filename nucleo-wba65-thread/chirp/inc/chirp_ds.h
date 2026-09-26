/* Parse validated Thread settings into a hardware-independent C structure.
 * chirp_prov maps the result to OpenThread dataset types. */
#ifndef CHIRP_DS_H
#define CHIRP_DS_H

/* Equals OT_OPERATIONAL_DATASET_MAX_LENGTH; a static assert in chirp_prov.c checks. */
#define CHIRP_DS_TLV_MAX  254u
#define CHIRP_DS_NAME_MAX 16u

typedef struct {
  int           have_tlv;
  unsigned char tlv[CHIRP_DS_TLV_MAX];
  unsigned      tlv_len;

  int           have_key;
  unsigned char key[16];

  int           have_name;
  char          name[CHIRP_DS_NAME_MAX + 1];   /* NUL-terminated */

  int           have_panid;
  unsigned      panid;                          /* 0..0xFFFF */

  int           have_xpanid;
  unsigned char xpanid[8];

  int           have_channel;
  unsigned      channel;                        /* 11..26 */
} chirp_ds_t;

/* Decode `n` hex digits into at most `outsz` bytes.
 * Returns the byte count, or -1 on odd length / non-hex / overflow. */
int chirp_hex2bin(const char *hex, unsigned n, unsigned char *out, unsigned outsz);

/* Extract the `thread` object.
 *   1 = a thread object was present and extracted
 *   0 = no thread object in this payload (Wi-Fi-only push)
 *  -1 = malformed beyond what validation rejects */
int chirp_ds_parse(const char *json, unsigned len, chirp_ds_t *out);

#endif /* CHIRP_DS_H */
