/* Host check for chirp_hwid.c: the announce says the id the reports use — the
 * die default until a config names one, the pushed hwid after — and is
 * CHIRP-PROV v1.8's line, with this demo's slug, fw and capabilities as
 * demo.json declares them. Run with make test, from this demo's folder.
 * Through v1.0.0 announce() printed the die id even on a provisioned board,
 * so the portal could not recognize a claimed Thread board. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chirp_hwid.h"
#include "json.h"

static int fails, passes;

static void expect(int cond, const char *why)
{
  printf("%s  %s\n", cond ? "ok  " : "FAIL", why);
  fails += !cond;
  passes += cond;
}

static char *read_file(const char *path)
{
  FILE *f = fopen(path, "rb");
  long  n;
  char *text;

  if (f == NULL) { return NULL; }
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  text = malloc((size_t)n + 1u);
  if (text != NULL && fread(text, 1, (size_t)n, f) == (size_t)n) { text[n] = '\0'; }
  else if (text != NULL) { free(text); text = NULL; }
  fclose(f);
  return text;
}

/* JSON text with the spaces outside strings taken out. */
static void compact(const char *in, unsigned n, char *out)
{
  int quoted = 0;
  for (unsigned i = 0; i < n; i++) {
    char c = in[i];
    if (c == '"' && (i == 0 || in[i - 1] != '\\')) { quoted = !quoted; }
    if (quoted || (c != ' ' && c != '\n' && c != '\r' && c != '\t')) { *out++ = c; }
  }
  *out = '\0';
}

int main(void)
{
  static const char pushed[] =
    "{\"host\":\"udp.warbletiot.com\",\"hwid\":\"example-board-0000\",\"token\":\"t\"}";
  static const char no_hwid[]  = "{\"host\":\"udp.warbletiot.com\"}";
  static const char blank[]    = "{\"hwid\":\"\"}";
  static const char longest[]  = "{\"hwid\":\"abcdefghij-abcdefghij-abcdefghij-abcdefghij-abc\"}";
  static char line[CHIRP_ANNOUNCE_MAX + 3u];
  char        tail[600], want[700], fw[80], slug[64], caps[512];
  char       *doc = read_file("demo.json");
  js_val      firmware, v;
  unsigned    n;

  expect(doc != NULL, "demo.json reads (run from the demo's folder)");
  fw[0] = slug[0] = caps[0] = '\0';
  if (doc != NULL && js_obj_get(doc, (unsigned)strlen(doc), "slug", &v) == 1) {
    (void)js_str_copy(&v, slug, sizeof slug);
  }
  if (doc != NULL && js_obj_get(doc, (unsigned)strlen(doc), "firmware", &firmware) == 1) {
    if (js_obj_get(firmware.p, firmware.n, "fw", &v) == 1) { (void)js_str_copy(&v, fw, sizeof fw); }
    if (js_obj_get(firmware.p, firmware.n, "capabilities", &v) == 1 && v.t == JS_OBJ &&
        v.n < sizeof caps) {
      compact(v.p, v.n, caps);
    }
  }
  if (fw[0] == '\0' || slug[0] == '\0' || caps[0] == '\0') {
    expect(0, "demo.json declares its slug, fw and capabilities");
    printf("\nFAILED (%d failure%s)\n", fails, fails == 1 ? "" : "s");
    return 1;
  }
  expect(strcmp(caps, CHIRP_CAPS) == 0, "CHIRP_CAPS is demo.json's capabilities");
  expect(strcmp(fw, CHIRP_FW) == 0, "the Makefile's FW_VERSION is demo.json's fw");
  snprintf(tail, sizeof tail, " fw=%s key_set=0 radios=thread t=%s caps=%s\r\n", fw, slug, caps);

  expect(strcmp(chirp_hwid(), "") == 0, "no id before setup");

  chirp_hwid_setup(0x3f9a12u);
  expect(strcmp(chirp_hwid(), "wba65-thread-3f9a12") == 0,
         "the die default before provisioning");
  n = chirp_announce_line(line, sizeof line, 0);
  snprintf(want, sizeof want, "CHIRP! v=2 hw=wba65-thread-3f9a12%s", tail);
  expect(n > 0u && strcmp(line, want) == 0,
         "the announce is v1.8's line: v=2 hw fw key_set radios t caps, caps last, as demo.json says");
  n = chirp_announce_line(line, sizeof line, 1);
  expect(n > 0u && strstr(line, " key_set=1 radios=") != NULL, "a signing key held: key_set=1");

  chirp_hwid_config(no_hwid, sizeof no_hwid - 1u);
  expect(strcmp(chirp_hwid(), "wba65-thread-3f9a12") == 0,
         "a config without hwid keeps the default");
  chirp_hwid_config(blank, sizeof blank - 1u);
  expect(strcmp(chirp_hwid(), "wba65-thread-3f9a12") == 0,
         "an empty hwid is not an id");

  chirp_hwid_config(pushed, sizeof pushed - 1u);
  expect(strcmp(chirp_hwid(), "example-board-0000") == 0,
         "the pushed hwid after provisioning");
  n = chirp_announce_line(line, sizeof line, 0);
  snprintf(want, sizeof want, "CHIRP! v=2 hw=example-board-0000%s", tail);
  expect(n > 0u && strcmp(line, want) == 0,
         "the announce carries the pushed id, not the die id");

  chirp_hwid_config(no_hwid, sizeof no_hwid - 1u);
  expect(strcmp(chirp_hwid(), "wba65-thread-3f9a12") == 0,
         "a config that no longer names an id puts the board back on the die default");

  /* The longest id this board holds still announces within 512 bytes. */
  chirp_hwid_config(longest, sizeof longest - 1u);
  n = chirp_announce_line(line, sizeof line, 1);
  expect(strlen(chirp_hwid()) == CHIRP_HWID_LEN_MAX && n > 0u && n - 2u <= CHIRP_ANNOUNCE_MAX,
         "the longest id announces within 512 bytes");

  expect(chirp_announce_line(line, 20u, 0) == 0u,
         "a buffer too small for the line gets no partial line");

  free(doc);
  printf("\nhwidtest: %d passed, %d failed\n", passes, fails);
  printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
