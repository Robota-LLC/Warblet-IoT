/* chirp_frame.c — see chirp_frame.h. No libc, no OpenThread, no hardware. */

#include "chirp_frame.h"

static unsigned string_length(const char *s)
{
  unsigned n = 0u;
  while (s[n] != '\0') { n++; }
  return n;
}

unsigned next_demo_temperature_tenths_c(unsigned *state)
{
  /* Increment simulated tenths; reset to 200 at the upper limit or for invalid state. */
  unsigned next = (*state >= 400u || *state < 200u) ? 200u : *state + 1u;
  *state = next;
  return next;
}

unsigned chirp_frame_payload(unsigned char *out, unsigned outsz, unsigned tenths)
{
  if (outsz < CHIRP_FRAME_PAYLOAD_LEN) { return 0u; }
  out[0] = (unsigned char)((tenths >> 8) & 0xFFu);
  out[1] = (unsigned char)(tenths & 0xFFu);
  return CHIRP_FRAME_PAYLOAD_LEN;
}

unsigned chirp_frame_build(char *out, unsigned outsz,
                           const char *hwid, const char *token, const char *sig_hex,
                           const unsigned char *payload, unsigned paylen)
{
  static const char protocol_prefix[] = "CHIRP1 ";

  /* Use "-" for an empty credential to keep the following fields in position. */
  const char *credential    = (token != 0 && token[0] != '\0') ? token : "-";
  const char *signature_hex = (sig_hex != 0 && sig_hex[0] != '\0') ? sig_hex : 0;

  unsigned hardware_id_length, token_length, signature_length;
  unsigned need, i, output_index;

  if (hwid == 0 || hwid[0] == '\0') { return 0u; }

  hardware_id_length = string_length(hwid);
  token_length       = string_length(credential);
  signature_length   = (signature_hex != 0) ? string_length(signature_hex) : 0u;

  need = (sizeof protocol_prefix - 1u) + hardware_id_length + 1u + token_length +
         (signature_hex ? 1u + signature_length : 0u) + 1u + paylen;
  if (need > outsz) { return 0u; }

  output_index = 0u;
  for (i = 0u; i < sizeof protocol_prefix - 1u; i++) { out[output_index++] = protocol_prefix[i]; }
  for (i = 0u; i < hardware_id_length; i++)          { out[output_index++] = hwid[i]; }
  out[output_index++] = ' ';
  for (i = 0u; i < token_length; i++)                { out[output_index++] = credential[i]; }
  if (signature_hex != 0) {
    out[output_index++] = ':';
    for (i = 0u; i < signature_length; i++)          { out[output_index++] = signature_hex[i]; }
  }
  out[output_index++] = ' ';
  for (i = 0u; i < paylen; i++)                      { out[output_index++] = (char)payload[i]; }
  return output_index;
}
