/* USB provisioning, config storage, and identity responses.
 * Thread settings can be stored, but this image does not join a network. */
#ifndef CHIRP_PROV_H
#define CHIRP_PROV_H

#define CHIRP_LINE_MAX 1200u

void chirp_prov_init(void);      /* banner + summary of any stored config */
void chirp_prov_poll(void);      /* drain the UART, act on complete lines  */

/* Room for an id: 63 characters and the NUL. */
#define CHIRP_HWID_SIZE 64u

/* The id this board transmits as: the pushed `hwid`, else `wba65-demo-` + the
 * low 24 bits of the die's unique id. NUL-terminated; returns the length. */
unsigned chirp_hwid(char *out, unsigned outsz);

#endif /* CHIRP_PROV_H */
