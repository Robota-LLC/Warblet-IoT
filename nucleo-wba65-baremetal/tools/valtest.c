/* Host checks for JSON, config validation, SHA-256, and HMAC.
 * Run with make test. */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "json.h"
#include "chirpval.h"
#include "sha256.h"
#include "chirp_sign.h"

static int fails;

static void hex_of(const unsigned char *d, unsigned n, char *out)
{
  static const char H[] = "0123456789abcdef";
  for (unsigned i = 0; i < n; i++) {
    out[i * 2] = H[d[i] >> 4];
    out[i * 2 + 1] = H[d[i] & 0xF];
  }
  out[n * 2] = '\0';
}

static void check_hex(const char *got, const char *want, const char *why)
{
  if (strcmp(got, want) != 0) {
    printf("FAIL  %s\n      want %s\n      got  %s\n", why, want, got);
    fails++;
  } else {
    printf("ok    %s\n", why);
  }
}

/* Config merge checks. */

/* Merge and hand back the result, failing loudly if the merge itself refused. */
static int merged(const char *stored, const char *push, char *out, unsigned outsz,
                  const char *why)
{
  int n = chirp_merge(stored, stored ? (unsigned)strlen(stored) : 0u,
                      push, (unsigned)strlen(push), out, outsz);
  if (n < 0) {
    printf("FAIL  %s: chirp_merge refused the merge\n", why);
    fails++;
  }
  return n;
}

/* The merged object's top-level string field `field` must equal `want`
 * (want == NULL means the field must be ABSENT). */
static void check_merge(const char *stored, const char *push,
                        const char *field, const char *want, const char *why)
{
  char out[1200], got[256];
  js_val v;
  int n = merged(stored, push, out, sizeof out, why);
  if (n < 0) return;

  if (js_obj_get(out, (unsigned)n, field, &v) != 1) {
    if (want == NULL) { printf("ok    %s\n", why); return; }
    printf("FAIL  %s: merged object has no %s\n      %s\n", why, field, out);
    fails++;
    return;
  }
  if (want == NULL) {
    printf("FAIL  %s: %s should be absent\n      %s\n", why, field, out);
    fails++;
    return;
  }
  if (js_str_copy(&v, got, sizeof got) < 0 || strcmp(got, want) != 0) {
    printf("FAIL  %s\n      want %s=%s\n      got  %s\n", why, field, want, out);
    fails++;
    return;
  }
  printf("ok    %s\n", why);
}

/* Presence-only, for fields that are not strings (thread, aps). */
static void check_merge_has(const char *stored, const char *push,
                            const char *field, int want, const char *why)
{
  char out[1200];
  js_val v;
  int n = merged(stored, push, out, sizeof out, why);
  if (n < 0) return;

  int has = js_obj_get(out, (unsigned)n, field, &v) == 1;
  if (has != want) {
    printf("FAIL  %s: %s is %s\n      %s\n", why, field,
           has ? "present" : "absent", out);
    fails++;
    return;
  }
  printf("ok    %s\n", why);
}

/* The merged record must pass the full rules, as the firmware asks before writing. */
static void check_merge_result(const char *stored, const char *push,
                               const char *want, const char *why)
{
  char out[1200];
  int n = merged(stored, push, out, sizeof out, why);
  if (n < 0) return;

  const char *got = chirp_validate(out, (unsigned)n);
  const char *g = got ? got : "ok";
  const char *w = want ? want : "ok";
  if (strcmp(g, w) != 0) {
    printf("FAIL  %-14s got %-14s %s\n      %s\n", w, g, why, out);
    fails++;
    return;
  }
  printf("ok    %-14s %s\n", g, why);
}

static void check_merge_ok(const char *stored, const char *push, const char *why)
{
  check_merge_result(stored, push, NULL, why);
}

static void check_merge_err(const char *stored, const char *push,
                            const char *want, const char *why)
{
  check_merge_result(stored, push, want, why);
}

static void check_sha(const char *msg, const char *want, const char *why)
{
  unsigned char d[SHA256_DIGEST];
  char got[2 * SHA256_DIGEST + 1];
  sha256(msg, (unsigned)strlen(msg), d);
  hex_of(d, SHA256_DIGEST, got);
  check_hex(got, want, why);
}

static void check(const char *json, const char *want, const char *why)
{
  const char *got = chirp_validate(json, (unsigned)strlen(json));
  const char *g = got ? got : "ok";
  const char *w = want ? want : "ok";
  if (strcmp(g, w) != 0) {
    printf("FAIL  %-14s got %-14s %s\n     %s\n", w, g, why, json);
    fails++;
  } else {
    printf("ok    %-14s %s\n", g, why);
  }
}

static void check_summary(const char *json)
{
  js_val t, v;
  unsigned len = (unsigned)strlen(json);
  char buf[64];

  if (js_obj_get(json, len, "thread", &t) != 1 || t.t != JS_OBJ) {
    printf("FAIL  summary: no thread object\n");
    fails++;
    return;
  }
  if (js_obj_get(t.p, t.n, "name", &v) == 1 && js_str_copy(&v, buf, sizeof buf) > 0) {
    printf("ok    summary name=%s\n", buf);
  }
  if (js_obj_get(t.p, t.n, "key", &v) == 1) {
    char fp[9];
    chirp_fingerprint(v.p, v.n, fp);
    printf("ok    summary key fingerprint=sha256:%s\n", fp);
  }
  if (js_obj_get(t.p, t.n, "channel", &v) == 1) {
    long ch = 0;
    if (js_num_int(&v, &ch) == 0) { printf("ok    summary channel=%ld\n", ch); }
  }
}

/* js_num_int must refuse, not wrap, a number past LONG_MAX. */
static void check_num(const char *text, int want_rc, long want, const char *why)
{
  js_val v;
  long got = 0;
  int rc;

  v.t = JS_NUM; v.p = text; v.n = (unsigned)strlen(text);
  rc = js_num_int(&v, &got);
  if (rc != want_rc || (rc == 0 && got != want)) {
    printf("FAIL  js_num_int(%s) = %d, %ld; want %d, %ld  %s\n", text, rc, got, want_rc, want, why);
    fails++;
  } else {
    printf("ok    js_num_int(%s) %s\n", text, why);
  }
}

static void test_num_limits(void)
{
  char max[32], over[32], tenfold[40], neg[40];

  snprintf(max, sizeof max, "%ld", LONG_MAX);
  snprintf(over, sizeof over, "%lu", (unsigned long)LONG_MAX + 1ul);
  snprintf(tenfold, sizeof tenfold, "%ld0", LONG_MAX);
  snprintf(neg, sizeof neg, "-%ld", LONG_MAX);

  puts("\n--- integer limits ---");
  check_num(max, 0, LONG_MAX, "LONG_MAX is accepted");
  check_num(neg, 0, -LONG_MAX, "-LONG_MAX is accepted");
  check_num(over, -1, 0, "LONG_MAX + 1 is refused, not wrapped");
  check_num(tenfold, -1, 0, "ten times LONG_MAX is refused");
  check_num("99999999999999999999", -1, 0, "twenty nines are refused");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899aabbccddeeff\","
        "\"channel\":4294967307}}", "bad-json", "channel 2^32 + 11 does not wrap to 11");
}

int main(void)
{
  const char *full =
    "{\"host\":\"my-core.example.com\",\"claim\":\"CHIRP-PLACEHOLDER\",\"hwid\":\"wba65-demo-3f9a12\","
    "\"level\":1,\"thread\":{\"name\":\"chirp-lab\",\"key\":\"00112233445566778899aabbccddeeff\","
    "\"panid\":\"1234\",\"xpanid\":\"1111111122222222\",\"channel\":15}}";

  const char *dataset =
    "{\"claim\":\"CHIRP-PLACEHOLDER\",\"thread\":{\"dataset\":\"0e080000000000010000000300001835"
    "060004001fffe00208111111112222222207081111111122222222051000112233445566778899aabbccddeeff"
    "030a63686972702d6c6162\"}}";

  puts("--- accepted ---");
  check(full, 0, "v1.2 thread, discrete fields");
  check(dataset, 0, "v1.2 thread, dataset form");
  check("{\"ssid\":\"workshop\",\"pass\":\"hunter2\",\"aps\":[{\"ssid\":\"workshop\",\"pass\":\"\"}],"
        "\"host\":\"h\",\"claim\":\"C\",\"level\":1}", 0, "v1.1 wifi shape still accepted");
  check("{\"token\":\"T\",\"ssid\":\"s\",\"pass\":\"\"}", 0, "token instead of claim");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899AABBCCDDEEFF\"},"
        "\"unknown\":{\"deep\":[1,2,3]}}", 0, "unknown keys ignored, uppercase key hex");
  check("{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e08\",\"name\":\"x\",\"key\":\"nope\"}}", 0,
        "dataset wins over broken discrete fields");
  check("{\"claim\":\"C\",\"ssid\":\"caf\\u00e9 \\\"guest\\\"\"}", 0, "escapes and \\u in strings");

  /* This relay image needs a credential but no network settings. */
  puts("\n--- fresh setup, no network (CHIRP_REQUIRE_NETWORK == 0) ---");
  check("{\"claim\":\"C\"}", 0, "a claim alone is a complete config for this image");
  check("{\"token\":\"T\"}", 0, "a token alone is a complete config for this image");
  check("{\"claim\":\"CHIRP-PLACEHOLDER\",\"hwid\":\"wba65-demo-3f9a12\",\"host\":\"h\","
        "\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\"}", 0,
        "what the setup page pushes at a fresh board: credential, no network");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"pass\":\"\"}", 0,
        "a credential beside a network is still accepted");

  puts("\n--- rejected ---");
  check("{\"claim\":\"X\",\"thread\":{", "bad-json", "truncated object");
  check("{\"host\":\"h\"}", "missing-field", "no claim/token");
  check("{\"ssid\":\"s\",\"pass\":\"p\"}", "missing-field",
        "a network without a credential is still refused");
  check("{\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\"}", "missing-field",
        "a signing key without a credential is still refused");
  check("{\"claim\":\"C\",\"token\":\"T\",\"ssid\":\"s\"}", "bad-json", "both claim and token");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\"}}", "missing-field", "thread name without key");
  check("{\"claim\":\"C\",\"thread\":{\"key\":\"00112233445566778899aabbccddeeff\"}}",
        "missing-field", "thread key without name");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233\"}}", "bad-json", "key too short");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899aabbccddeegg\"}}",
        "bad-json", "key not hex");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"0123456789abcdefg\",\"key\":\"00112233445566778899aabbccddeeff\"}}",
        "bad-json", "name longer than 16");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899aabbccddeeff\",\"channel\":27}}",
        "bad-json", "channel above 26");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899aabbccddeeff\",\"channel\":10}}",
        "bad-json", "channel below 11");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899aabbccddeeff\",\"panid\":\"12g4\"}}",
        "bad-json", "panid not hex");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899aabbccddeeff\",\"xpanid\":\"1111\"}}",
        "bad-json", "xpanid wrong length");
  check("{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e0\"}}", "bad-json", "odd-length dataset");
  check("{\"claim\":\"C\",\"thread\":\"not-an-object\"}", "bad-json", "thread must be an object");
  check("{\"claim\":123,\"ssid\":\"s\"}", "bad-json", "claim must be a string");
  check("[1,2,3]", "bad-json", "not an object");
  check("{\"claim\":\"C\",\"ssid\":\"s\",}", "bad-json", "trailing comma");
  check("{'claim':'C'}", "bad-json", "single quotes");
  check("{\"claim\":\"C\",\"ssid\":\"s\"} trailing", "bad-json", "junk after the object");
  check("{\"claim\":\"C\",\"thread\":{\"name\":\"n\",\"key\":\"00112233445566778899aabbccddeeff\"}} {}",
        "bad-json", "two objects on one line");

  puts("\n--- v1.5 signing key ---");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\"}",
        0, "44-char base64 key accepted");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8\"}",
        "bad-json", "key without the '=' pad");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"key\":\"c2hvcnQ=\"}",
        "bad-json", "key that is not 32 bytes");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdH!8=\"}",
        "bad-json", "key with a character outside base64");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"key\":42}", "bad-json", "key must be a string");

  puts("\n--- v1.3 merges (the portal's L2 elevation flow) ---");
  {
    /* Check key-only pushes and pushes containing both a key and a replacement
     * token. */
    const char *provisioned =
      "{\"token\":\"TOK-ONE\",\"ssid\":\"workshop\",\"pass\":\"hunter2\","
      "\"thread\":{\"name\":\"chirp-lab\",\"key\":\"00112233445566778899aabbccddeeff\"}}";
    const char *key_only = "{\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\"}";
    const char *key_tok  = "{\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\","
                           "\"token\":\"TOK-TWO\"}";

    check(key_only, "missing-field", "a key-only push is NOT a complete config");
    printf("      ^ which is why completeness is judged on the merge, not the push\n");

    check_merge(provisioned, key_only, "token", "TOK-ONE",
                "key-only push keeps the stored token");
    check_merge(provisioned, key_only, "ssid", "workshop",
                "key-only push keeps the stored ssid");
    check_merge(provisioned, key_only, "key",
                "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=",
                "key-only push lands the key");
    check_merge_has(provisioned, key_only, "thread", 1,
                    "key-only push keeps the whole thread object");
    check_merge_ok(provisioned, key_only,
                   "merged record round-trips through chirp_validate");

    check_merge(provisioned, key_tok, "token", "TOK-TWO",
                "key+token push replaces the credential");
    check_merge_ok(provisioned, key_tok, "key+token merge is still valid");

    /* A rotation pushes a token; the stale claim must not survive beside it. */
    check_merge("{\"claim\":\"CHIRP-OLD\",\"ssid\":\"s\"}",
                "{\"token\":\"TOK-NEW\"}", "claim", NULL,
                "a token push evicts the stored claim code");
    check_merge_ok("{\"claim\":\"CHIRP-OLD\",\"ssid\":\"s\"}",
                   "{\"token\":\"TOK-NEW\"}", "credential swap stays valid");

    /* A single supplied network replaces the stored list. */
    check_merge_has("{\"token\":\"T\",\"ssid\":\"old\",\"pass\":\"p\","
                    "\"aps\":[{\"ssid\":\"old\",\"pass\":\"p\"},{\"ssid\":\"van\",\"pass\":\"\"}]}",
                    "{\"ssid\":\"new\",\"pass\":\"n\"}", "aps", 0,
                    "a single-ssid push replaces the whole network list");
    check_merge("{\"token\":\"T\",\"ssid\":\"old\",\"pass\":\"p\"}",
                "{\"ssid\":\"new\",\"pass\":\"n\"}", "ssid", "new",
                "...and the new ssid wins");

    /* Nothing stored: a merged view with no credential is still refused. */
    check_merge_err(NULL, key_only, "missing-field",
                    "first-ever key-only push is still refused");
    check_merge_ok(NULL, "{\"token\":\"T\"}",
                   "first push with a credential and no network is accepted");
    check_merge_ok(NULL,
                   "{\"claim\":\"CHIRP-PLACEHOLDER\",\"hwid\":\"wba65-demo-3f9a12\","
                   "\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\"}",
                   "empty store + the setup page's push (no network) is accepted");
    check_merge_ok(NULL, "{\"token\":\"T\",\"ssid\":\"s\",\"pass\":\"\"}",
                   "first push with a credential and a network is accepted");
    check_merge_err(NULL, "{\"ssid\":\"s\",\"pass\":\"\"}", "missing-field",
                    "first push with a network but no credential is refused");

    /* The splice must not mangle what it copies. */
    check_merge("{\"token\":\"T\",\"ssid\":\"caf\\u00e9 \\\"guest\\\"\"}",
                "{\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\"}",
                "ssid", "caf\xc3\xa9 \"guest\"",
                "escapes and \\u survive the merge byte for byte");
    check_merge_has("{\"token\":\"T\",\"ssid\":\"s\"}",
                    "{\"thread\":{\"dataset\":\"0e08\"}}", "thread", 1,
                    "a thread-only push lands against a wifi board");
  }

  puts("\n--- sha256 (FIPS 180-4 examples) ---");
  check_sha("abc",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "sha256(\"abc\")");
  check_sha("",
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "sha256(\"\") — the empty message");
  check_sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
            "sha256(56 bytes) — padding spills into a second block");
  check_sha("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
            "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
            "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
            "sha256(112 bytes) — two full blocks");

  puts("\n--- hmac-sha256 (RFC 4231) ---");
  {
    unsigned char key[131], data[50];
    char got[65];
    unsigned i;

    const char *d1 = "Hi There";
    const char *d2 = "what do ya want for nothing?";
    const char *d6 = "Test Using Larger Than Block-Size Key - Hash Key First";

    for (i = 0; i < 20u; i++) { key[i] = 0x0b; }
    chirp_sign(key, 20u, d1, (unsigned)strlen(d1), got);
    check_hex(got, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
              "RFC 4231 case 1");

    chirp_sign((const unsigned char *)"Jefe", 4u, d2, (unsigned)strlen(d2), got);
    check_hex(got, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
              "RFC 4231 case 2 — key shorter than the block");

    for (i = 0; i < 25u; i++) { key[i] = (unsigned char)(i + 1u); }
    for (i = 0; i < 50u; i++) { data[i] = 0xcd; }
    chirp_sign(key, 25u, data, 50u, got);
    check_hex(got, "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
              "RFC 4231 case 4");

    for (i = 0; i < 131u; i++) { key[i] = 0xaa; }
    chirp_sign(key, 131u, d6, (unsigned)strlen(d6), got);
    check_hex(got, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
              "RFC 4231 case 6 — key longer than the block");
  }

  puts("\n--- the demo's own L2 frame (the README quotes this) ---");
  {
    /* Example key: bytes 0x00 through 0x1f. Never use it as a device credential. */
    unsigned char key[32], body[2];
    char got[65];
    unsigned i;
    for (i = 0; i < 32u; i++) { key[i] = (unsigned char)i; }
    body[0] = 0x00; body[1] = 0xc9;                       /* 201 tenths = 20.1 C */
    chirp_sign(key, 32u, body, 2u, got);
    printf("ok    X-Chirp-Signature over body 00c9 = %s\n", got);
  }

  puts("\n--- summary fields (what the boot line prints) ---");
  check_summary(full);

  test_num_limits();

  printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
