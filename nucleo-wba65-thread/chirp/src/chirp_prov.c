/* Handle USB identity, config pushes, and DFU commands.
 * Validate and merge settings, save them, then apply the Thread dataset.
 * The linker wraps CLI initialization and UART input to attach this responder.
 * Schedule auto-join after ST initialization, which otherwise disables Thread. */

#include "board_serial.h"
#include "chirp_ds.h"
#include "chirp_dfu.h"
#include "chirp_report.h"
#include "chirp_sign.h"
#include "chirpval.h"
#include "flashcfg.h"
#include "json.h"

#include "stm32wbaxx.h"     /* UID_BASE */

#include "app_conf.h"       /* CFG_TASK_NBR, CFG_SEQ_PRIO_1 */
#include "stm32_seq.h"
#include "stm32_timer.h"

#include "platform_wba.h"   /* LL_LOCK / LL_UNLOCK */

#include "openthread/cli.h"
#include "openthread/dataset.h"
#include "openthread/error.h"
#include "openthread/instance.h"
#include "openthread/ip6.h"
#include "openthread/thread.h"

/* chirp_ds.h knows nothing about OpenThread; the two sizes meet here. */
_Static_assert(CHIRP_DS_TLV_MAX == OT_OPERATIONAL_DATASET_MAX_LENGTH,
               "chirp_ds TLV buffer must match OT_OPERATIONAL_DATASET_MAX_LENGTH");

/* Scheduler task ids above ST's CFG_TASK_NBR. board_serial.c owns 29 and
 * chirp_report.c owns 28. */
#define CHIRP_TASK_VERIFY_ID  30u
#define CHIRP_TASK_JOIN_ID    31u
#define CHIRP_TASK_VERIFY     (1u << CHIRP_TASK_VERIFY_ID)
#define CHIRP_TASK_JOIN       (1u << CHIRP_TASK_JOIN_ID)

_Static_assert(CHIRP_TASK_VERIFY_ID >= (unsigned)CFG_TASK_NBR,
               "chirp scheduler task ids overlap ST's CFG_Task_Id_t");
_Static_assert(CHIRP_TASK_JOIN_ID < 32u, "sequencer task ids are 0..31");

/* Delay before reading the Thread state back: long enough for a scan window. */
#define CHIRP_VERIFY_MS 3000u

/* --wrap partner, provided by the linker. */
void __real_otCliInit(otInstance *aInstance, otCliOutputCallback aCallback, void *aContext);

static otInstance *s_inst;

/* Keep the running config aligned with flash and use it as the next merge base. */
static char     s_cfg[CFG_MAX_PAYLOAD + 1];
static unsigned s_cfg_len;
static int      s_cfg_present;

/* Merge scratch: a merge that turns out too big must leave s_cfg untouched. */
static char     s_merged[CFG_MAX_PAYLOAD + 1];

/* Boot join runs exactly once per boot however often the task is set. */
static int s_join_ran;

/* Read-back timer, and its "what was I verifying" tag. */
static UTIL_TIMER_Object_t s_verify_tmr;
static int                 s_verify_made;
static const char         *s_verify_tag = "state";

/* Role changes are printed until the first attach, then quiet. */
static int s_role_quiet;

/* ------------------------------------------------------------------ small str */

static int str_eq(const char *a, const char *b)
{
  while (*a && *a == *b) { a++; b++; }
  return *a == '\0' && *b == '\0';
}

static int str_starts_with(const char *s, const char *prefix)
{
  while (*prefix) { if (*s++ != *prefix++) { return 0; } }
  return 1;
}

/* ------------------------------------------------------------------ hardware id */

/* Low 24 bits of UID word 0: the per-die part of the unique id. */
static unsigned hardware_id(void)
{
  return (*(volatile const unsigned *)UID_BASE) & 0xFFFFFFu;
}

/* ------------------------------------------------------------------ summary */

/* Print a hashed fingerprint, never any part of the credential itself. */
static void print_key_fingerprint(const char *secret, unsigned n)
{
  char fp[9];

  if (chirp_sign_fingerprint(fp, secret, n) != 0) { board_serial_print("????"); return; }
  board_serial_print("sha256:%s", fp);
}

static void summarize(const char *json, unsigned len)
{
  js_val v, t;

  board_serial_print("CHIRP. cfg");

  if (js_obj_get(json, len, "host", &v) == 1 && v.t == JS_STR) {
    board_serial_print(" host=%.*s", (int)v.n, v.p);
  }
  if (js_obj_get(json, len, "ssid", &v) == 1 && v.t == JS_STR) {
    board_serial_print(" ssid=%.*s", (int)v.n, v.p);
  }

  if (js_obj_get(json, len, "key", &v) == 1 && v.t == JS_STR) {
    board_serial_print(" signkey=");
    print_key_fingerprint(v.p, v.n);
    board_serial_print(" (L2)");
  } else {
    board_serial_print(" nosignkey (L1)");
  }

  if (js_obj_get(json, len, "thread", &t) == 1 && t.t == JS_OBJ) {
    if (js_obj_get(t.p, t.n, "dataset", &v) == 1 && v.t == JS_STR) {
      board_serial_print(" thread=dataset(%uB) fp=", v.n / 2u);
      print_key_fingerprint(v.p, v.n);
    } else {
      if (js_obj_get(t.p, t.n, "name", &v) == 1 && v.t == JS_STR) {
        board_serial_print(" thread=%.*s", (int)v.n, v.p);
      }
      if (js_obj_get(t.p, t.n, "key", &v) == 1 && v.t == JS_STR) {
        board_serial_print(" key=");
        print_key_fingerprint(v.p, v.n);
      }
      if (js_obj_get(t.p, t.n, "channel", &v) == 1 && v.t == JS_NUM) {
        long ch = 0;
        if (js_num_int(&v, &ch) == 0) { board_serial_print(" ch=%u", (unsigned)ch); }
      }
      if (js_obj_get(t.p, t.n, "panid", &v) == 1 && v.t == JS_STR) {
        board_serial_print(" panid=%.*s", (int)v.n, v.p);
      }
    }
  }
  board_serial_print("\r\n");
}

/* ------------------------------------------------------------------ state truth */

static void report_state(const char *tag)
{
  otDeviceRole role;

  if (s_inst == NULL) { return; }

  role = otThreadGetDeviceRole(s_inst);
  board_serial_print("CHIRP. %s check: ip6=%s role=%s\r\n", tag,
                     otIp6IsEnabled(s_inst) ? "up" : "down",
                     otThreadDeviceRoleToString(role));

  if (role == OT_DEVICE_ROLE_DISABLED) {
    board_serial_print(
      "CHIRP. thread is DISABLED - it did not stay enabled; this is a fault\r\n");
  }
}

/* Scheduler task, so it may call OpenThread. */
static void chirp_verify_task(void)
{
  LL_LOCK();
  report_state(s_verify_tag);
  LL_UNLOCK();
}

/* Timer interrupt context: only set a task. */
static void chirp_verify_timer_cb(void *arg)
{
  (void)arg;
  UTIL_SEQ_SetTask(CHIRP_TASK_VERIFY, CFG_SEQ_PRIO_1);
}

static void arm_verify(const char *tag)
{
  s_verify_tag = tag;

  if (!s_verify_made) {
    if (UTIL_TIMER_Create(&s_verify_tmr, CHIRP_VERIFY_MS, UTIL_TIMER_ONESHOT,
                          chirp_verify_timer_cb, NULL) != UTIL_TIMER_OK) {
      board_serial_print(
        "CHIRP. no verify timer available; check with 'state' by hand\r\n");
      return;
    }
    s_verify_made = 1;
  }

  (void)UTIL_TIMER_Stop(&s_verify_tmr);
  if (UTIL_TIMER_Start(&s_verify_tmr) != UTIL_TIMER_OK) {
    board_serial_print(
      "CHIRP. verify timer refused to start; check with 'state' by hand\r\n");
  }
}

/* Log the first role change after applying settings. */
static void chirp_state_cb(uint32_t flags, void *ctx)
{
  otDeviceRole role;

  (void)ctx;
  if ((flags & (uint32_t)OT_CHANGED_THREAD_ROLE) == 0u) { return; }
  if (s_role_quiet || s_inst == NULL)                   { return; }

  role = otThreadGetDeviceRole(s_inst);
  board_serial_print("CHIRP. role %s\r\n", otThreadDeviceRoleToString(role));

  if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER ||
      role == OT_DEVICE_ROLE_LEADER) {
    s_role_quiet = 1;                    /* attached: stop narrating */
  }
}

/* ------------------------------------------------------------------ OT apply */

/* Apply the dataset: 2 if already active, 1 if applied, -1 on error. */
static int apply_dataset(const chirp_ds_t *ds, const char **why, otError *perr)
{
  otOperationalDatasetTlvs want;
  otOperationalDatasetTlvs have;
  otOperationalDataset     d;
  otError                  err;

  *perr = OT_ERROR_NONE;
  if (s_inst == NULL) { *why = "no-stack"; *perr = OT_ERROR_INVALID_STATE; return -1; }

  /* Convert to TLVs so either form can be compared with the active dataset. */
  if (ds->have_tlv) {
    want.mLength = (uint8_t)ds->tlv_len;
    for (unsigned i = 0; i < ds->tlv_len; i++) { want.mTlvs[i] = ds->tlv[i]; }
  } else {
    unsigned char *p = (unsigned char *)&d;
    for (unsigned i = 0; i < sizeof d; i++) { p[i] = 0u; }

    /* An active timestamp is mandatory for a dataset OpenThread will accept. */
    d.mActiveTimestamp.mSeconds             = 1;
    d.mComponents.mIsActiveTimestampPresent = true;

    if (ds->have_channel) {
      d.mChannel = (uint16_t)ds->channel;
      d.mComponents.mIsChannelPresent = true;
    }
    if (ds->have_panid) {
      d.mPanId = (otPanId)ds->panid;
      d.mComponents.mIsPanIdPresent = true;
    }
    if (ds->have_xpanid) {
      for (unsigned i = 0; i < 8u; i++) { d.mExtendedPanId.m8[i] = ds->xpanid[i]; }
      d.mComponents.mIsExtendedPanIdPresent = true;
    }
    if (ds->have_key) {
      for (unsigned i = 0; i < 16u; i++) { d.mNetworkKey.m8[i] = ds->key[i]; }
      d.mComponents.mIsNetworkKeyPresent = true;
    }
    if (ds->have_name) {
      unsigned i = 0;
      for (; i < CHIRP_DS_NAME_MAX && ds->name[i]; i++) { d.mNetworkName.m8[i] = ds->name[i]; }
      d.mNetworkName.m8[i] = '\0';
      d.mComponents.mIsNetworkNamePresent = true;
    }
    otDatasetConvertToTlvs(&d, &want);
  }

  /* Leave an identical dataset on a running stack connected. */
  if (otThreadGetDeviceRole(s_inst) != OT_DEVICE_ROLE_DISABLED &&
      otDatasetGetActiveTlvs(s_inst, &have) == OT_ERROR_NONE &&
      have.mLength == want.mLength) {
    unsigned same = 1u;
    for (unsigned i = 0; i < want.mLength; i++) {
      if (have.mTlvs[i] != want.mTlvs[i]) { same = 0u; break; }
    }
    if (same) { return 2; }
  }

  /* Bring the stack down before swapping: OpenThread does not promise to
   * handle a live dataset change. */
  if (otThreadGetDeviceRole(s_inst) != OT_DEVICE_ROLE_DISABLED) {
    (void)otThreadSetEnabled(s_inst, false);
  }
  (void)otIp6SetEnabled(s_inst, false);

  err = ds->have_tlv ? otDatasetSetActiveTlvs(s_inst, &want)
                     : otDatasetSetActive(s_inst, &d);

  if (err != OT_ERROR_NONE) { *why = "dataset-rejected"; *perr = err; return -1; }

  err = otIp6SetEnabled(s_inst, true);
  if (err != OT_ERROR_NONE) { *why = "ip6-enable-failed"; *perr = err; return -1; }

  err = otThreadSetEnabled(s_inst, true);
  if (err != OT_ERROR_NONE) { *why = "thread-enable-failed"; *perr = err; return -1; }

  s_role_quiet = 0;                      /* narrate this attach */
  return 1;
}

/* Apply whatever is in `json`.
 *   2 = the dataset was already active and the stack is already up
 *   1 = dataset applied and the stack enabled
 *   0 = payload carried no `thread` object, so there was nothing to join
 *  -1 = failed; *why and *perr say why */
static int apply_json(const char *json, unsigned len, const char **why, otError *perr)
{
  chirp_ds_t ds;
  int        r = chirp_ds_parse(json, len, &ds);

  *perr = OT_ERROR_NONE;
  if (r < 0)  { *why = "bad-json"; *perr = OT_ERROR_PARSE; return -1; }
  if (r == 0) { return 0; }

  return apply_dataset(&ds, why, perr);
}

/* ------------------------------------------------------------------ commands */

static void announce(void)
{
  /* Announce Thread and signing support, plus this demo's slug. */
  board_serial_print("CHIRP! v=2 hw=wba65-thread-%06x radios=thread sig=1 t=demo-nucleo-wba65-thread\r\n",
                     hardware_id());
}

/* Validate supplied fields, merge, check completeness, store, then apply. */
static void handle_config(char *json, unsigned len)
{
  static const char empty[] = "{}";
  const char *err;
  const char *why   = "apply-failed";
  otError     oterr = OT_ERROR_NONE;
  unsigned    mlen, i;
  int         applied;

  /* 1. Field shape, judged on the push alone. */
  err = chirp_validate_fields(json, len);
  if (err) { board_serial_print("CHIRP= err %s\r\n", err); return; }

  /* 2. MERGE over what is stored. */
  mlen = chirp_merge(s_merged, sizeof s_merged,
                     s_cfg_present ? s_cfg : empty,
                     s_cfg_present ? s_cfg_len : (unsigned)(sizeof empty - 1u),
                     json, len);
  if (mlen == 0u || mlen > CFG_MAX_PAYLOAD) {
    board_serial_print("CHIRP= err store-failed\r\n");
    return;
  }

  /* 3. Completeness, judged on the merged result. */
  err = chirp_validate(s_merged, mlen);
  if (err) { board_serial_print("CHIRP= err %s\r\n", err); return; }

  /* 4. Store before claiming ok. */
  if (cfg_store(s_merged, mlen) != 0) {
    board_serial_print("CHIRP= err store-failed\r\n");
    return;
  }

  /* The stored record is the next merge base. */
  for (i = 0u; i < mlen; i++) { s_cfg[i] = s_merged[i]; }
  s_cfg[mlen]   = '\0';
  s_cfg_len     = mlen;
  s_cfg_present = 1;

  json = s_cfg;
  len  = mlen;

  /* 5. Apply. The reporter follows the stored record, as a reboot would. */
  chirp_report_config(json, len);

  applied = apply_json(json, len, &why, &oterr);

  if (applied < 0) {
    /* Stored but not joinable: say both halves. */
    board_serial_print("CHIRP= err apply-failed\r\n");
    board_serial_print("CHIRP. apply failed: %s (%s)\r\n", why,
                       otThreadErrorToString(oterr));
    board_serial_print(
      "CHIRP. stored to 0x%08x but OpenThread refused it; fix and push again\r\n",
      cfg_page_addr());
    return;
  }

  board_serial_print("CHIRP= ok\r\n");
  summarize(json, len);

  if (applied == 0) {
    board_serial_print("CHIRP. no thread object in this push; stack left as it was\r\n");
    return;
  }

  if (applied == 2) {
    board_serial_print("CHIRP. dataset unchanged; thread never stopped\r\n");
  } else {
    board_serial_print("CHIRP. new dataset; thread enabled\r\n");
  }
  board_serial_print("CHIRP. reading the state back in %us\r\n",
                     CHIRP_VERIFY_MS / 1000u);
  arm_verify("push");
}

/* Every complete CHIRP line, from board_serial.c's scheduler task. */
static void handle_chirp_line(char *line, unsigned len)
{
  while (len && (line[len - 1] == ' ' || line[len - 1] == '\t' ||
                 line[len - 1] == '\r' || line[len - 1] == '\n')) {
    line[--len] = '\0';
  }
  if (len == 0u) { return; }

  if (str_eq(line, "CHIRP?")) { announce(); return; }

  if (str_starts_with(line, "CHIRP+ ")) {
    char    *json = line + 7;
    unsigned jl   = len - 7u;
    while (jl && (*json == ' ' || *json == '\t')) { json++; jl--; }
    if (jl == 0u) { board_serial_print("CHIRP= err bad-json\r\n"); return; }
    handle_config(json, jl);
    return;
  }

  if (str_eq(line, "CHIRP~ dfu")) {
    board_serial_print("CHIRP~ ok dfu\r\n");
    board_serial_print("CHIRP. stopping thread + radio, then jumping to 0x0BF90000\r\n");
    board_serial_print("CHIRP. if 0483:df11 does not enumerate, power-cycle the board\r\n");
    if (s_verify_made) { (void)UTIL_TIMER_Stop(&s_verify_tmr); }
    board_serial_drain();
    chirp_dfu_enter(s_inst);
    return;                              /* unreachable */
  }

  board_serial_print("CHIRP= err unsupported\r\n");
}

/* ------------------------------------------------------------------ boot */

/* Runs inside __wrap_otCliInit. Reads flash and prints; touches no OT state. */
static void chirp_banner(void)
{
  board_serial_print("\r\nCHIRP. thread_chirp boot hw=wba65-thread-%06x cfgpage=0x%08x\r\n",
                     hardware_id(), cfg_page_addr());

  if (cfg_load(s_cfg, sizeof s_cfg, &s_cfg_len) != 0) {
    s_cfg_present = 0;
    board_serial_print("CHIRP. cfg none - stock ot-cli, no auto-join\r\n");
  } else {
    s_cfg_present = 1;
    summarize(s_cfg, s_cfg_len);
    board_serial_print(
      "CHIRP. auto-join queued on the sequencer (after APP_THREAD_DeviceConfig)\r\n");
  }

  board_serial_print("CHIRP. ready (CHIRP? / CHIRP+ <json> / CHIRP~ dfu)\r\n");
}

/* Join once from the scheduler, after ST's initialization has disabled Thread. */
static void chirp_join_task(void)
{
  const char *why = "apply-failed";
  otError     oterr = OT_ERROR_NONE;
  int         applied;

  if (s_join_ran) { return; }
  s_join_ran = 1;

  if (s_inst == NULL) { return; }

  LL_LOCK();

  /* After ST's own handlers; a full callback table only loses role printing. */
  if (otSetStateChangedCallback(s_inst, chirp_state_cb, NULL) != OT_ERROR_NONE) {
    board_serial_print(
      "CHIRP. (no free state-changed slot; role changes will not be printed)\r\n");
  }

  if (!s_cfg_present) { LL_UNLOCK(); return; }

  applied = apply_json(s_cfg, s_cfg_len, &why, &oterr);

  LL_UNLOCK();

  if (applied < 0) {
    board_serial_print("CHIRP. auto-join failed: %s (%s)\r\n", why,
                       otThreadErrorToString(oterr));
    board_serial_print(
      "CHIRP. the stored config is still on the device; fix it and push again\r\n");
    return;
  }
  if (applied == 0) {
    board_serial_print("CHIRP. stored config has no thread object; nothing to join\r\n");
    return;
  }

  board_serial_print("CHIRP. auto-join %s; reading the state back in %us\r\n",
                     (applied == 2) ? "already active" : "enabled",
                     CHIRP_VERIFY_MS / 1000u);
  arm_verify("auto-join");
}

/* ------------------------------------------------------------------ the hook */

/* Attach serial reception and register tasks after CLI initialization. */
void __wrap_otCliInit(otInstance *aInstance, otCliOutputCallback aCallback, void *aContext)
{
  __real_otCliInit(aInstance, aCallback, aContext);
  s_inst = aInstance;

  chirp_banner();

  /* Queue initialization work for execution after MX_APPE_Init returns. */
  UTIL_SEQ_RegTask(CHIRP_TASK_JOIN,   UTIL_SEQ_RFU, chirp_join_task);
  UTIL_SEQ_RegTask(CHIRP_TASK_VERIFY, UTIL_SEQ_RFU, chirp_verify_task);
  chirp_report_setup(aInstance);
  UTIL_SEQ_SetTask(CHIRP_TASK_JOIN, CFG_SEQ_PRIO_1);

  /* The reporter's first run, and its timer, happen on the scheduler after
   * APP_THREAD_DeviceConfig(). */
  if (s_cfg_present) { chirp_report_config(s_cfg, s_cfg_len); }

  board_serial_begin(handle_chirp_line);
}
