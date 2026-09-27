/* The device id, shared by the announce and the reports. Hardware-free: the
 * die id comes in as a number, the pushed id as config JSON. */
#include <stdio.h>

#include "chirp_hwid.h"
#include "json.h"

static char s_hwid[CHIRP_HWID_MAX];

void chirp_hwid_setup(unsigned uid24)
{
  (void)snprintf(s_hwid, sizeof s_hwid, "wba65-thread-%06x", uid24 & 0xFFFFFFu);
}

void chirp_hwid_config(const char *json, unsigned len)
{
  js_val   v;
  char     id[CHIRP_HWID_MAX];
  unsigned i;

  /* Copied whole or not at all: a pushed id that does not fit, or an empty
   * one, leaves the current id in place. */
  if (js_obj_get(json, len, "hwid", &v) != 1 || v.t != JS_STR ||
      js_str_copy(&v, id, sizeof id) <= 0) {
    return;
  }
  for (i = 0u; (s_hwid[i] = id[i]) != '\0'; i++) { }
}

const char *chirp_hwid(void)
{
  return s_hwid;
}

unsigned chirp_announce_line(char *out, unsigned outsz)
{
  /* Thread and signing support, this demo's slug, and the id the reports use. */
  int n = snprintf(out, outsz,
                   "CHIRP! v=2 hw=%s radios=thread sig=1 t=demo-nucleo-wba65-thread\r\n",
                   s_hwid);

  if (n <= 0 || (unsigned)n >= outsz) { return 0u; }
  return (unsigned)n;
}
