/* Host checks for JSON, config merging, datasets, UDP frames, and signing.
 * Run with make test. */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "json.h"
#include "chirpval.h"
#include "chirp_ds.h"
#include "chirp_frame.h"
#include "chirp_sign.h"
#include "mbedtls/sha256.h"   /* the host stand-in in chirp/test/ */

static int fails;

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
    printf("ok    summary key present (%u hex chars)\n", v.n);
  }
  if (js_obj_get(t.p, t.n, "channel", &v) == 1) {
    long ch = 0;
    if (js_num_int(&v, &ch) == 0) { printf("ok    summary channel=%ld\n", ch); }
  }
}

/* Config merge checks. */

/* The portal's USB key delivery pushes `{"key":...}` alone and relies on the
 * responder keeping everything else. */

static char g_merged[1024];

static const char *mrg(const char *cur, const char *push)
{
  unsigned n = chirp_merge(g_merged, sizeof g_merged,
                           cur, (unsigned)strlen(cur),
                           push, (unsigned)strlen(push));
  if (n == 0u) { return "<refused>"; }
  g_merged[n] = '\0';
  return g_merged;
}

static void mcheck(const char *cur, const char *push, const char *want, const char *why)
{
  const char *got = mrg(cur, push);
  if (strcmp(got, want) != 0) {
    printf("FAIL  merge: %s\n  got  %s\n  want %s\n", why, got, want);
    fails++;
  } else {
    printf("ok    merge: %s\n", why);
  }
}

/* Validate push fields before merging, then validate the merged config. */
static void mvalid(const char *cur, const char *push, const char *want, const char *why)
{
  const char *got = chirp_validate_fields(push, (unsigned)strlen(push));

  if (!got) {
    const char *merged = mrg(cur, push);
    got = chirp_validate(merged, (unsigned)strlen(merged));
    if (!got) { got = "ok"; }
  }
  if (strcmp(got, want ? want : "ok") != 0) {
    printf("FAIL  v1.3 %-14s got %-14s %s\n", want ? want : "ok", got, why);
    fails++;
  } else {
    printf("ok    v1.3 %-14s %s\n", got, why);
  }
}

#define KEY44 "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="

static void test_merge(void)
{
  /* What this board actually holds after a normal provision. */
  static const char provisioned[] =
    "{\"token\":\"tok-old\",\"hwid\":\"curious-otter-1a2b\","
    "\"thread\":{\"dataset\":\"0e08aabb\"}}";

  puts("\n--- v1.3 merges ---");

  /* A key-only push preserves the other stored settings. */
  mcheck(provisioned, "{\"key\":\"" KEY44 "\"}",
         "{\"key\":\"" KEY44 "\",\"token\":\"tok-old\",\"hwid\":\"curious-otter-1a2b\","
         "\"thread\":{\"dataset\":\"0e08aabb\"}}",
         "key-only push keeps token, hwid and the thread dataset");
  mvalid(provisioned, "{\"key\":\"" KEY44 "\"}", 0,
         "key-only push is complete once merged");

  /* (2) key + token: the post-rotation push. The dead token must not survive. */
  mcheck(provisioned, "{\"key\":\"" KEY44 "\",\"token\":\"tok-new\"}",
         "{\"key\":\"" KEY44 "\",\"token\":\"tok-new\",\"hwid\":\"curious-otter-1a2b\","
         "\"thread\":{\"dataset\":\"0e08aabb\"}}",
         "key+token push replaces the credential and keeps the network");
  if (strstr(g_merged, "tok-old") == NULL) {
    printf("ok    merge: the rotated-away token is gone, not sitting beside the new one\n");
  } else {
    printf("FAIL  merge: a dead token survived a rotation push\n");
    fails++;
  }

  /* (3) A swap across the claim/token boundary: they are alternatives, so the
   * stored one has to go or the merged result is `bad-json`. */
  mcheck("{\"claim\":\"CHIRP-OLD\",\"ssid\":\"s\"}", "{\"token\":\"T\"}",
         "{\"token\":\"T\",\"ssid\":\"s\"}",
         "a token push clears a stored claim (v1.1: at most one)");
  mvalid("{\"claim\":\"CHIRP-OLD\",\"ssid\":\"s\"}", "{\"token\":\"T\"}", 0,
         "claim -> token swap stays valid");

  /* (4) First push at a blank board: completeness lands on the push itself. */
  mvalid("{}", "{\"key\":\"" KEY44 "\"}", "missing-field",
         "key-only push at a NEVER-provisioned board is refused");
  mvalid("{}", "{\"token\":\"T\"}", "missing-field",
         "credential alone at a blank board has nothing to join");
  mvalid("{}", "{\"ssid\":\"s\"}", "missing-field",
         "network alone at a blank board has no credential");
  mvalid("{}", "{\"token\":\"T\",\"ssid\":\"s\"}", 0,
         "a complete first push is accepted");

  /* A single supplied ssid replaces the stored network list. */
  mcheck("{\"token\":\"T\",\"ssid\":\"old\",\"pass\":\"p\","
         "\"aps\":[{\"ssid\":\"old\",\"pass\":\"p\"},{\"ssid\":\"other\",\"pass\":\"\"}]}",
         "{\"ssid\":\"only\",\"pass\":\"q\"}",
         "{\"ssid\":\"only\",\"pass\":\"q\",\"token\":\"T\"}",
         "a single top-level ssid with no aps replaces the stored list");

  /* (6) Values are spliced, never re-emitted. */
  mcheck("{\"token\":\"T\",\"ssid\":\"caf\\u00e9 \\\"guest\\\"\",\"level\":2}",
         "{\"hwid\":\"h\"}",
         "{\"hwid\":\"h\",\"token\":\"T\",\"ssid\":\"caf\\u00e9 \\\"guest\\\"\",\"level\":2}",
         "escapes and numbers survive verbatim");

  /* (7) A merge that will not fit is refused whole. */
  if (chirp_merge(g_merged, 16u, provisioned, (unsigned)strlen(provisioned),
                  "{\"hwid\":\"h\"}", 14u) == 0u) {
    printf("ok    merge: refuses to overflow rather than truncating a config\n");
  } else {
    printf("FAIL  merge: overflowed\n");
    fails++;
  }

  /* Reject malformed signing keys before merging. */
  mvalid(provisioned, "{\"key\":\"not-base64\"}", "bad-json",
         "a malformed signing key is bad-json, not silently ignored");
  mvalid(provisioned, "{\"key\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8\"}", "bad-json",
         "a 43-character key (padding stripped) is not a 32-byte key");
  mvalid(provisioned, "{\"key\":\"" KEY44 "\",\"claim\":\"C\",\"token\":\"T\"}", "bad-json",
         "claim and token together is still refused on the push");
}

/* ---------------------------------------------------------------- chirp_ds.c */

static void ds_expect(const char *json, int want_rc, const char *why)
{
  chirp_ds_t ds;
  int rc = chirp_ds_parse(json, (unsigned)strlen(json), &ds);
  if (rc != want_rc) {
    printf("FAIL  ds rc=%d want %d  %s\n     %s\n", rc, want_rc, why, json);
    fails++;
  } else {
    printf("ok    ds rc=%-2d %s\n", rc, why);
  }
}

static void ds_field(int cond, const char *why)
{
  if (cond) { printf("ok    ds %s\n", why); }
  else      { printf("FAIL  ds %s\n", why); fails++; }
}

static void hex_expect(const char *hex, int want, const char *why)
{
  unsigned char buf[8];
  int got = chirp_hex2bin(hex, (unsigned)strlen(hex), buf, sizeof buf);
  if (got != want) {
    printf("FAIL  hex got %d want %d  %s (%s)\n", got, want, why, hex);
    fails++;
  } else {
    printf("ok    hex %-3d %s\n", got, why);
  }
}

static void test_ds(const char *full, const char *dataset)
{
  chirp_ds_t ds;

  puts("\n--- chirp_hex2bin ---");
  hex_expect("00ff", 2, "lowercase");
  hex_expect("00FF", 2, "uppercase");
  hex_expect("0", -1, "odd length");
  hex_expect("0g", -1, "not hex");
  hex_expect("", -1, "empty");
  hex_expect("00112233445566778899", -1, "overflows an 8-byte buffer");
  hex_expect("0011223344556677", 8, "exactly fills the buffer");

  puts("\n--- chirp_ds_parse ---");
  ds_expect(full, 1, "discrete fields extracted");
  ds_expect(dataset, 1, "dataset form extracted");
  ds_expect("{\"claim\":\"C\",\"ssid\":\"s\"}", 0, "no thread object");

  /* discrete form: every field lands where chirp_prov.c expects it */
  memset(&ds, 0, sizeof ds);
  chirp_ds_parse(full, (unsigned)strlen(full), &ds);
  ds_field(!ds.have_tlv,                        "discrete: no TLV blob");
  ds_field(ds.have_key && ds.key[0] == 0x00 && ds.key[15] == 0xff, "discrete: key bytes");
  ds_field(ds.have_name && strcmp(ds.name, "chirp-lab") == 0,      "discrete: name");
  ds_field(ds.have_panid && ds.panid == 0x1234u,                   "discrete: panid 0x1234");
  ds_field(ds.have_xpanid && ds.xpanid[0] == 0x11 && ds.xpanid[7] == 0x22, "discrete: xpanid");
  ds_field(ds.have_channel && ds.channel == 15u,                   "discrete: channel 15");

  /* dataset form: TLV decoded, and it must shadow the discrete fields */
  memset(&ds, 0, sizeof ds);
  chirp_ds_parse(dataset, (unsigned)strlen(dataset), &ds);
  ds_field(ds.have_tlv,                         "dataset: TLV present");
  ds_field(ds.tlv_len == 72u,                   "dataset: 72 bytes decoded");
  ds_field(ds.tlv[0] == 0x0e && ds.tlv[1] == 0x08, "dataset: first TLV is Active Timestamp");
  {
    /* Check that the dataset's type-length-value entries end at the buffer boundary. */
    unsigned i = 0, n = 0;
    while (i + 2u <= ds.tlv_len) { i += 2u + ds.tlv[i + 1u]; n++; }
    ds_field(i == ds.tlv_len && n == 7u, "dataset: TLV chain walks to exactly 7 TLVs");
  }
  ds_field(!ds.have_key && !ds.have_name,       "dataset: discrete fields not populated");

  /* dataset wins even over broken discrete fields, as in chirpval.c */
  memset(&ds, 0, sizeof ds);
  ds_expect("{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e08\",\"name\":\"x\",\"key\":\"nope\"}}",
            1, "dataset wins over broken discrete fields");
  chirp_ds_parse("{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e08\",\"name\":\"x\",\"key\":\"nope\"}}",
                 55u + 12u, &ds);

  /* things validation lets through that the extractor must still refuse */
  ds_expect("{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e0\"}}", -1, "odd-length dataset refused");
  ds_expect("{\"claim\":\"C\",\"thread\":{\"key\":\"00112233\"}}", -1, "short key refused");

  /* Valid hex can contain an invalid TLV length. OpenThread must reject this dataset. */
  {
    const char *ds_malformed =
      "{\"claim\":\"C\",\"thread\":{\"dataset\":\"0e080000000000010000000300001835"
      "060004001fffe00208111111112222222207081111111122222222051000112233445566778899aabbccddeeff"
      "030a63686972702d6c6162\"}}";
    chirp_ds_t bad;
    unsigned   i = 0, n = 0;

    check(ds_malformed, 0, "TLV-malformed dataset still passes validation (hex-only check)");
    ds_expect(ds_malformed, 1, "TLV-malformed dataset still decodes to bytes");
    chirp_ds_parse(ds_malformed, (unsigned)strlen(ds_malformed), &bad);
    while (i + 2u <= bad.tlv_len) { i += 2u + bad.tlv[i + 1u]; n++; }
    ds_field(i > bad.tlv_len, "TLV-malformed dataset overruns its chain (OT will refuse it)");
  }
}

/* The wire bytes are the contract; pin them byte for byte. */
static void test_frame(void)
{
  /* A placeholder digest: the layout does not depend on its value. test_sign()
   * checks a real signature in a real datagram. */
  static const char sig[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  unsigned char payload[CHIRP_FRAME_PAYLOAD_LEN];
  char          buf[192];
  unsigned      n, pn, state;

  puts("\n--- udp frame ---");

  pn = chirp_frame_payload(payload, sizeof payload, 214u);
  if (pn == 2u && payload[0] == 0x00u && payload[1] == 0xd6u) {
    printf("ok    payload: 21.4C -> 00 d6, big-endian tenths\n");
  } else {
    printf("FAIL  payload: 21.4C gave %u bytes\n", pn);
    fails++;
  }

  n = chirp_frame_build(buf, sizeof buf, "hw-01", "tok9", NULL, payload, pn);
  if (n == 20u && memcmp(buf, "CHIRP1 hw-01 tok9 \x00\xd6", 20u) == 0) {
    printf("ok    frame: L1, token alone\n");
  } else {
    printf("FAIL  frame: L1 form (n=%u)\n", n);
    fails++;
  }

  n = chirp_frame_build(buf, sizeof buf, "hw-01", "tok9", sig, payload, pn);
  if (n == 85u &&
      memcmp(buf, "CHIRP1 hw-01 tok9:", 18u) == 0 &&
      memcmp(buf + 18, sig, 64u) == 0 &&
      memcmp(buf + 82, " \x00\xd6", 3u) == 0) {
    printf("ok    frame: L2, token:hex in the SAME field, payload untouched\n");
  } else {
    printf("FAIL  frame: L2 form (n=%u)\n", n);
    fails++;
  }

  n = chirp_frame_build(buf, sizeof buf, "hw-01", "tok9", "", payload, pn);
  if (n == 20u && memcmp(buf, "CHIRP1 hw-01 tok9 \x00\xd6", 20u) == 0) {
    printf("ok    frame: an empty signature is no signature\n");
  } else {
    printf("FAIL  frame: empty signature (n=%u)\n", n);
    fails++;
  }

  pn = chirp_frame_payload(payload, sizeof payload, 400u);
  n  = chirp_frame_build(buf, sizeof buf, "hw-01", "", NULL, payload, pn);
  if (n == 17u && memcmp(buf, "CHIRP1 hw-01 - \x01\x90", 17u) == 0) {
    printf("ok    frame: empty token becomes the L0 marker\n");
  } else {
    printf("FAIL  frame: L0 form (n=%u)\n", n);
    fails++;
  }

  if (chirp_frame_build(buf, sizeof buf, "", "t", NULL, payload, pn) == 0u) {
    printf("ok    frame: empty hwid refused\n");
  } else {
    printf("FAIL  frame: empty hwid accepted\n");
    fails++;
  }

  if (chirp_frame_build(buf, 10u, "a-very-long-hardware-id", "t", NULL, payload, pn) == 0u) {
    printf("ok    frame: refuses to overflow the buffer\n");
  } else {
    printf("FAIL  frame: overflowed\n");
    fails++;
  }

  if (chirp_frame_build(buf, 24u, "hw-01", "tok9", sig, payload, pn) == 0u) {
    printf("ok    frame: a signature that would not fit is refused, not truncated\n");
  } else {
    printf("FAIL  frame: truncated a signature\n");
    fails++;
  }

  state = 200u;
  n = next_demo_temperature_tenths_c(&state);
  if (n == 201u) { printf("ok    ramp: first reading is 20.1C\n"); }
  else           { printf("FAIL  ramp: first reading %u\n", n); fails++; }

  state = 400u;
  n = next_demo_temperature_tenths_c(&state);
  if (n == 200u) { printf("ok    ramp: wraps 40.0C -> 20.0C\n"); }
  else           { printf("FAIL  ramp: wrap gave %u\n", n); fails++; }

  state = 0u;
  n = next_demo_temperature_tenths_c(&state);
  if (n == 200u) { printf("ok    ramp: out-of-band state restarts the ramp\n"); }
  else           { printf("FAIL  ramp: bad state gave %u\n", n); fails++; }
}

/* Signing checks. */

/* The contract's example key, the 32 ASCII bytes "0123456789abcdef" twice, in
 * the base64 form the config stores. Visibly not a real credential. */
static const char vector_key_b64[] = "MDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWY=";

#define BODY_MAX 64u

static void hex_of(const unsigned char *d, unsigned n, char *out)
{
  static const char H[] = "0123456789abcdef";
  unsigned i;
  for (i = 0u; i < n; i++) {
    out[i * 2u]      = H[d[i] >> 4];
    out[i * 2u + 1u] = H[d[i] & 0xFu];
  }
  out[n * 2u] = '\0';
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

/* SHA-256 through the host stand-in, so the signer below is not trusted blind. */
static void check_sha(const char *msg, const char *want, const char *why)
{
  mbedtls_sha256_context c;
  unsigned char          d[32];
  char                   got[65];
  int                    rc;

  mbedtls_sha256_init(&c);
  rc = mbedtls_sha256_starts(&c, 0);
  if (rc == 0) { rc = mbedtls_sha256_update(&c, (const unsigned char *)msg, strlen(msg)); }
  if (rc == 0) { rc = mbedtls_sha256_finish(&c, d); }
  mbedtls_sha256_free(&c);
  if (rc != 0) {
    printf("FAIL  %s: sha256 returned %d\n", why, rc);
    fails++;
    return;
  }
  hex_of(d, 32u, got);
  check_hex(got, want, why);
}

/* chirp_sign_hex straight over msg, no prefix: the HMAC itself. */
static void check_hmac(const char *key_b64, const char *msg, const char *want, const char *why)
{
  char got[CHIRP_SIGN_HEX_LEN + 1];

  if (chirp_sign_hex(got, sizeof got, key_b64, (unsigned)strlen(key_b64),
                     (const unsigned char *)msg, (unsigned)strlen(msg)) != 0) {
    printf("FAIL  %s: chirp_sign_hex refused\n", why);
    fails++;
    return;
  }
  check_hex(got, want, why);
}

/* Build the signed input the way chirp_report.c does (chirp_sign_prefix, then
 * the payload), check those bytes as hex, then check the HMAC from
 * chirp_sign_hex. NULL means the message has no such field. */
static void check_signed(const char *key_b64, const char *nonce, const char *tag,
                         const void *body, unsigned blen,
                         const char *want_input_hex, const char *want_hmac,
                         const char *why)
{
  unsigned char input[CHIRP_SIGN_PREFIX_MAX + BODY_MAX];
  char          hex[2u * sizeof input + 1u];
  char          got[CHIRP_SIGN_HEX_LEN + 1];
  int           plen = chirp_sign_prefix((char *)input, CHIRP_SIGN_PREFIX_MAX, nonce, tag);

  if (plen < 0 || blen > BODY_MAX) {
    printf("FAIL  %s: the signed input did not fit\n", why);
    fails++;
    return;
  }
  memcpy(input + plen, body, blen);
  hex_of(input, (unsigned)plen + blen, hex);
  if (strcmp(hex, want_input_hex) != 0) {
    printf("FAIL  %s: signed input\n      want %s\n      got  %s\n", why, want_input_hex, hex);
    fails++;
    return;
  }
  if (chirp_sign_hex(got, sizeof got, key_b64, (unsigned)strlen(key_b64),
                     input, (unsigned)plen + blen) != 0) {
    printf("FAIL  %s: chirp_sign_hex refused\n", why);
    fails++;
    return;
  }
  check_hex(got, want_hmac, why);
}

static void test_sign(void)
{
  static const char nonce[] = "1757332800123";
  static const char body[]  = "temp=21.4";
  static const char moved[] = "1757332800123\ntag=snap\ntemp=21.4";

  puts("\n--- sha-256 host stand-in (FIPS 180-4 examples) ---");
  check_sha("abc",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "sha256(\"abc\")");
  check_sha("",
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "sha256(\"\"), the empty message");
  check_sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
            "sha256(56 bytes), padding spills into a second block");

  puts("\n--- hmac-sha256 through chirp_sign_hex (RFC 4231) ---");
  check_hmac("CwsLCwsLCwsLCwsLCwsLCwsLCws=", "Hi There",
             "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
             "RFC 4231 case 1");
  check_hmac("SmVmZQ==", "what do ya want for nothing?",
             "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
             "RFC 4231 case 2, key shorter than the block");

  puts("\n--- signed input: <nonce> \\n tag=<tag> \\n <body> (contract examples) ---");
  check_signed(vector_key_b64, nonce, "snap", body, 9u,
               "313735373333323830303132330a7461673d736e61700a74656d703d32312e34",
               "1239148f97a361adfb7173445a84a68a4f0b3eb915d626b98e1080f7e6f78e81",
               "nonce and tag");
  check_signed(vector_key_b64, NULL, NULL, body, 9u,
               "0a7461673d0a74656d703d32312e34",
               "fd071ea6328d4ca04bf766d76c4298359a07db6b161fbc8446b336511954bda2",
               "neither: \"\\ntag=\\n\" + body, the shape this demo signs");
  check_signed(vector_key_b64, "", "", body, 9u,
               "0a7461673d0a74656d703d32312e34",
               "fd071ea6328d4ca04bf766d76c4298359a07db6b161fbc8446b336511954bda2",
               "empty strings sign the same as no fields");
  check_signed(vector_key_b64, NULL, "snap", body, 9u,
               "0a7461673d736e61700a74656d703d32312e34",
               "aaa23e9e54a70f6c76ff1bfdc282831c3c62c0d574f592fdd831b554bef3e946",
               "tag only: the empty nonce line is still there");
  check_signed(vector_key_b64, NULL, NULL, "", 0u,
               "0a7461673d0a",
               "e33be001a2833da36aae83c191aa790e65ef7d07bbc061f9b236eed063e0eb38",
               "empty body: only the two lines are signed");
  check_signed(vector_key_b64, NULL, NULL, "a\nb\n", 4u,
               "0a7461673d0a610a620a",
               "cb339ac72c21c9ce109fe397965bacd56fdc35ca8be8f2aad1dd0eb2e1dd311d",
               "multi-line body is signed byte for byte");
  check_signed(vector_key_b64, NULL, NULL, moved, (unsigned)strlen(moved),
               "0a7461673d0a313735373333323830303132330a7461673d736e61700a74656d703d32312e34",
               "054bf69a4a1eb145077ab94da1e61c29fff03f98e8d79d4f2352157b9177a32c",
               "the nonce and tag lines moved into the body");

  /* Moving the nonce and tag lines into the body must change the signature. */
  {
    unsigned char a[CHIRP_SIGN_PREFIX_MAX + BODY_MAX], b[CHIRP_SIGN_PREFIX_MAX + BODY_MAX];
    char          ha[CHIRP_SIGN_HEX_LEN + 1], hb[CHIRP_SIGN_HEX_LEN + 1];
    int           pa = chirp_sign_prefix((char *)a, CHIRP_SIGN_PREFIX_MAX, nonce, "snap");
    int           pb = chirp_sign_prefix((char *)b, CHIRP_SIGN_PREFIX_MAX, NULL, NULL);

    if (pa < 0 || pb < 0) {
      printf("FAIL  substitution: chirp_sign_prefix refused a prefix it has room for\n");
      fails++;
    } else {
      unsigned na = (unsigned)pa + 9u, nb = (unsigned)pb + (unsigned)strlen(moved);
      memcpy(a + pa, body, 9u);
      memcpy(b + pb, moved, strlen(moved));
      if (chirp_sign_hex(ha, sizeof ha, vector_key_b64, sizeof vector_key_b64, a, na) != 0 ||
          chirp_sign_hex(hb, sizeof hb, vector_key_b64, sizeof vector_key_b64, b, nb) != 0) {
        printf("FAIL  substitution: chirp_sign_hex refused\n");
        fails++;
      } else if ((na == nb && memcmp(a, b, na) == 0) || strcmp(ha, hb) == 0) {
        printf("FAIL  substitution: fields moved into the body sign the same\n");
        fails++;
      } else {
        printf("ok    substitution: fields moved into the body sign differently\n");
      }
    }
  }

  /* The prefix refuses a buffer that is too small instead of cutting it short. */
  {
    char     small[8], big[CHIRP_SIGN_PREFIX_MAX];
    char     longest_nonce[CHIRP_NONCE_MAX + 1u], longest_tag[CHIRP_TAG_MAX + 1u];
    unsigned i;

    for (i = 0u; i < CHIRP_NONCE_MAX; i++) { longest_nonce[i] = '9'; }
    longest_nonce[CHIRP_NONCE_MAX] = '\0';
    for (i = 0u; i < CHIRP_TAG_MAX; i++) { longest_tag[i] = 't'; }
    longest_tag[CHIRP_TAG_MAX] = '\0';

    if (chirp_sign_prefix(small, 6u, NULL, NULL) == -1 &&
        chirp_sign_prefix(small, 7u, NULL, NULL) == 6 &&
        chirp_sign_prefix(big, sizeof big, longest_nonce, longest_tag) ==
          (int)(CHIRP_NONCE_MAX + CHIRP_TAG_MAX + 6u)) {
      printf("ok    prefix: refuses a short buffer, fits the longest nonce and tag\n");
    } else {
      printf("FAIL  prefix: buffer bounds\n");
      fails++;
    }
  }

  puts("\n--- the demo's own L2 datagram (the README quotes this) ---");
  {
    /* README example key: bytes 0x00 through 0x1f. Never use it as a device
     * credential. Payload 00 d6 is 214 tenths = 21.4 C. */
    static const char demo_key[] = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
    static const char want_sig[] =
      "d5c7080b4958b335e0e45c159209f4c88bdeea2f3f7e99c5dfbe25c8c05e3322";
    unsigned char payload[CHIRP_FRAME_PAYLOAD_LEN];
    unsigned char input[CHIRP_SIGN_PREFIX_MAX + CHIRP_FRAME_PAYLOAD_LEN];
    char          sig[CHIRP_SIGN_HEX_LEN + 1], buf[192];
    unsigned      pn = chirp_frame_payload(payload, sizeof payload, 214u);
    unsigned      n = 0u;
    int           plen;

    check_signed(demo_key, NULL, NULL, payload, pn, "0a7461673d0a00d6", want_sig,
                 "signature over \"\\ntag=\\n\" + payload 00d6");

    /* The same steps chirp_report.c takes, through to the datagram. */
    plen = chirp_sign_prefix((char *)input, CHIRP_SIGN_PREFIX_MAX, 0, 0);
    if (plen >= 0) {
      memcpy(input + plen, payload, pn);
      if (chirp_sign_hex(sig, sizeof sig, demo_key, sizeof demo_key,
                         input, (unsigned)plen + pn) == 0) {
        n = chirp_frame_build(buf, sizeof buf, "hw-01", "tok9", sig, payload, pn);
      }
    }
    if (n == 85u &&
        memcmp(buf, "CHIRP1 hw-01 tok9:", 18u) == 0 &&
        memcmp(buf + 18, want_sig, 64u) == 0 &&
        memcmp(buf + 82, " \x00\xd6", 3u) == 0) {
      printf("ok    datagram: CHIRP1 hw-01 tok9:%s <00 d6>\n", want_sig);
    } else {
      printf("FAIL  datagram: L2 frame with the real signature (n=%u)\n", n);
      fails++;
    }
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

/* A pushed hwid is the platform's grammar or it is refused: it goes into the
 * announce line, a topic and a URL, so a space or a line break must not. */
static void test_hwid(void)
{
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"hwid\":\"example-board-0000\"}", 0,
        "hwid: a portal id");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"hwid\":\"A.b_c:9-z\"}", 0,
        "hwid: every allowed punctuation");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"hwid\":\"\"}", 0,
        "hwid: empty means the die's default");
  check("{\"claim\":\"C\",\"ssid\":\"s\","
        "\"hwid\":\"0123456789abcdef0123456789abcdef0123456789abcde\"}", 0,
        "hwid: 47 characters fit");
  check("{\"claim\":\"C\",\"ssid\":\"s\","
        "\"hwid\":\"0123456789abcdef0123456789abcdef0123456789abcdef\"}", "bad-json",
        "hwid: 48 characters do not fit");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"hwid\":\"two words\"}", "bad-json",
        "hwid: a space");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"hwid\":\"id\\r\\nCHIRP! v=2 hw=other\"}", "bad-json",
        "hwid: a line break cannot forge an announce");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"hwid\":\"caf\\u00e9\"}", "bad-json",
        "hwid: an escape");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"hwid\":\"a/b\"}", "bad-json",
        "hwid: a slash");
  check("{\"claim\":\"C\",\"ssid\":\"s\",\"hwid\":7}", "bad-json",
        "hwid: not a string");
}

int main(void)
{
  const char *full =
    "{\"host\":\"my-core.example.com\",\"claim\":\"CHIRP-PLACEHOLDER\",\"hwid\":\"wba65-demo-3f9a12\","
    "\"level\":1,\"thread\":{\"name\":\"chirp-lab\",\"key\":\"00112233445566778899aabbccddeeff\","
    "\"panid\":\"1234\",\"xpanid\":\"1111111122222222\",\"channel\":15}}";

  /* A 72-byte dataset with seven complete TLVs.
   * The ds_malformed fixture exercises an invalid Network Name length. */
  const char *dataset =
    "{\"claim\":\"CHIRP-PLACEHOLDER\",\"thread\":{\"dataset\":\"0e080000000000010000000300001835"
    "060004001fffe00208111111112222222207081111111122222222051000112233445566778899aabbccddeeff"
    "030963686972702d6c6162\"}}";

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

  puts("\n--- rejected ---");
  check("{\"claim\":\"X\",\"thread\":{", "bad-json", "truncated object");
  check("{\"host\":\"h\"}", "missing-field", "no claim/token");
  check("{\"claim\":\"C\"}", "missing-field", "claim but no ssid and no thread");
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

  puts("\n--- summary fields (what the boot line prints) ---");
  check_summary(full);

  test_hwid();
  test_num_limits();

  test_merge();
  test_ds(full, dataset);
  test_frame();
  test_sign();

  printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}
