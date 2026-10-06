/* Host check for chirp_hwid.c: the announce says the id the reports use — the
 * die default until a config names one, the pushed hwid after. Run with make
 * test. Through v1.0.0 announce() printed the die id even on a provisioned
 * board, so the portal could not recognize a claimed Thread board. */

#include <stdio.h>
#include <string.h>

#include "chirp_hwid.h"

static int fails;

static void expect(int cond, const char *why)
{
  printf("%s  %s\n", cond ? "ok  " : "FAIL", why);
  fails += !cond;
}

#define TAIL " radios=thread sig=1 t=demo-nucleo-wba65-thread\r\n"

int main(void)
{
  static const char pushed[] =
    "{\"host\":\"udp.warbletiot.com\",\"hwid\":\"example-board-0000\",\"token\":\"t\"}";
  static const char no_hwid[]  = "{\"host\":\"udp.warbletiot.com\"}";
  static const char blank[]    = "{\"hwid\":\"\"}";
  static const char forty[]    = "{\"hwid\":\"abcdefghij-abcdefghij-abcdefghij-abcdefg\"}";
  char     line[256];
  unsigned n;

  expect(strcmp(chirp_hwid(), "") == 0, "no id before setup");

  chirp_hwid_setup(0x3f9a12u);
  expect(strcmp(chirp_hwid(), "wba65-thread-3f9a12") == 0,
         "the die default before provisioning");
  n = chirp_announce_line(line, sizeof line);
  expect(n > 0u && strcmp(line, "CHIRP! v=2 hw=wba65-thread-3f9a12" TAIL) == 0,
         "the announce carries the default id");

  chirp_hwid_config(no_hwid, sizeof no_hwid - 1u);
  expect(strcmp(chirp_hwid(), "wba65-thread-3f9a12") == 0,
         "a config without hwid keeps the default");
  chirp_hwid_config(blank, sizeof blank - 1u);
  expect(strcmp(chirp_hwid(), "wba65-thread-3f9a12") == 0,
         "an empty hwid is not an id");

  chirp_hwid_config(pushed, sizeof pushed - 1u);
  expect(strcmp(chirp_hwid(), "example-board-0000") == 0,
         "the pushed hwid after provisioning");
  n = chirp_announce_line(line, sizeof line);
  expect(n > 0u && strcmp(line, "CHIRP! v=2 hw=example-board-0000" TAIL) == 0,
         "the announce carries the pushed id, not the die id");

  /* A long id (40 characters) still announces inside one 256-byte serial line. */
  chirp_hwid_config(forty, sizeof forty - 1u);
  n = chirp_announce_line(line, sizeof line);
  expect(strlen(chirp_hwid()) == 40u && n > 0u && n < 256u,
         "a 40-character id announces within one serial line");

  expect(chirp_announce_line(line, 20u) == 0u,
         "a buffer too small for the line gets no partial line");

  printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
