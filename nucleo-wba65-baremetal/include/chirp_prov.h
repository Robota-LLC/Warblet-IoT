/* USB provisioning, config storage, and identity responses.
 * Thread settings can be stored, but this image does not join a network. */
#ifndef CHIRP_PROV_H
#define CHIRP_PROV_H

#include "chirp_profile.h"

/* A setup line is never longer, counted from its "CHIRP"; a longer one is
 * thrown away whole, through its line ending. The longest legal push is about
 * 3.2 KB. */
#define CHIRP_LINE_MAX 4096u

void chirp_prov_init(void);              /* banner + summary of any stored config */
void chirp_prov_poll(unsigned now_ms);   /* drain the UART, act on complete lines  */

/* Room for an id and its NUL. */
#define CHIRP_HWID_SIZE (CHIRP_HWID_LEN_MAX + 1u)

/* The id this board transmits as: the pushed `hwid`, else `wba65-demo-` + the
 * low 24 bits of the die's unique id. NUL-terminated; returns the length. */
unsigned chirp_hwid(char *out, unsigned outsz);

#endif /* CHIRP_PROV_H */
