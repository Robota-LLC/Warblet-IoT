/* Build HTTP requests and print them on the ST-LINK console for a computer relay.
 * Stored credentials add headers; a signing key adds an HMAC signature.
 * This image has no IP or TLS stack. */
#ifndef CHIRP_SEND_H
#define CHIRP_SEND_H

void chirp_send_init(void);        /* read the stored credentials, say the level */
void chirp_send_reload(void);      /* re-read them after a CHIRP+ push           */
void chirp_send_tick(unsigned now_ms);  /* one uplink every 30 s                 */

#endif /* CHIRP_SEND_H */
