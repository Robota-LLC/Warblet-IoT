/* Build the unsigned two-byte temperature payload and its UDP datagram.
 * The reporter signs the payload between these two steps. */
#ifndef CHIRP_FRAME_H
#define CHIRP_FRAME_H

/* Bytes chirp_frame_payload() writes. */
#define CHIRP_FRAME_PAYLOAD_LEN 2u

/* Simulated temperature ramp in tenths; initialize state to 200. */
unsigned next_demo_temperature_tenths_c(unsigned *state);

/* Two big-endian bytes of tenths: all a decoder ever sees. Swap this for your
 * sensor. Returns the byte count, or 0 if it would not fit. */
unsigned chirp_frame_payload(unsigned char *out, unsigned outsz, unsigned tenths);

/* Build CHIRP1 <hwid> <token-field> <payload>.
 * Use token:sig_hex when signed, and '-' for an empty token.
 * Return total length, or 0 for an empty hardware ID or insufficient space. */
unsigned chirp_frame_build(char *out, unsigned outsz,
                           const char *hwid, const char *token, const char *sig_hex,
                           const unsigned char *payload, unsigned paylen);

#endif /* CHIRP_FRAME_H */
