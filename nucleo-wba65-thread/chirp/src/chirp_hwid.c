/* The device id, shared by the announce and the reports. Hardware-free: the
 * die id comes in as a number, the pushed id as config JSON. */
#include <stdio.h>

#include "chirp_hwid.h"
#include "json.h"

/* fw=: the release in which this demo's firmware last changed. The Makefile
 * passes it (FW_VERSION), and demo.json's firmware.fw says the same. */
#ifndef CHIRP_FW
#error "build with the Makefile: it passes CHIRP_FW, this demo's FW_VERSION"
#endif

static char s_die[CHIRP_HWID_MAX];
static char s_hwid[CHIRP_HWID_MAX];

void chirp_hwid_setup(unsigned uid24)
{
  unsigned i;

  (void)snprintf(s_die, sizeof s_die, "wba65-thread-%06x", uid24 & 0xFFFFFFu);
  for (i = 0u; (s_hwid[i] = s_die[i]) != '\0'; i++) { }
}

void chirp_hwid_config(const char *json, unsigned len)
{
  js_val      v;
  char        id[CHIRP_HWID_MAX];
  const char *from = s_die;
  unsigned    i;

  /* A pushed id is copied whole or not at all; a config with none, or an
   * empty one, leaves the board on its own id. */
  if (js_obj_get(json, len, "hwid", &v) == 1 && v.t == JS_STR &&
      js_str_copy(&v, id, sizeof id) > 0) {
    from = id;
  }
  for (i = 0u; (s_hwid[i] = from[i]) != '\0'; i++) { }
}

const char *chirp_hwid(void)
{
  return s_hwid;
}

unsigned chirp_announce_line(char *out, unsigned outsz, int key_set)
{
  /* CHIRP-PROV v1.8: v=2, it takes a Thread dataset; what it can join; this
   * demo's slug; and caps= last, the one value that holds JSON. */
  int n = snprintf(out, outsz,
                   "CHIRP! v=2 hw=%s fw=%s key_set=%d radios=thread t=demo-nucleo-wba65-thread caps=%s\r\n",
                   s_hwid, CHIRP_FW, key_set ? 1 : 0, CHIRP_CAPS);

  if (n <= 0 || (unsigned)n >= outsz || (unsigned)n - 2u > CHIRP_ANNOUNCE_MAX) { return 0u; }
  return (unsigned)n;
}
