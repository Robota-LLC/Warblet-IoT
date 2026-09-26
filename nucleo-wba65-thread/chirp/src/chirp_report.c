/* Schedule a report every 30 seconds, resolve the host, then send a UDP datagram.
 * Run OpenThread calls on the sequencer or in OpenThread callbacks under LL_LOCK.
 * Log failures when their cause changes and log recovery once. */

#include <stdio.h>

#include "board_serial.h"

#include "chirp_frame.h"
#include "chirp_report.h"
#include "chirp_sign.h"
#include "json.h"

#include "stm32wbaxx.h"     /* UID_BASE */

#include "app_conf.h"       /* CFG_TASK_NBR, CFG_SEQ_PRIO_1 */
#include "stm32_seq.h"
#include "stm32_timer.h"

#include "platform_wba.h"   /* LL_LOCK / LL_UNLOCK */

#include "openthread/dns_client.h"
#include "openthread/error.h"
#include "openthread/message.h"
#include "openthread/thread.h"
#include "openthread/udp.h"

/* Task id 28; board_serial.c owns 29 and chirp_prov.c 30..31. */
#define CHIRP_TASK_REPORT_ID 28u
#define CHIRP_TASK_REPORT    (1u << CHIRP_TASK_REPORT_ID)
_Static_assert(CHIRP_TASK_REPORT_ID >= (unsigned)CFG_TASK_NBR,
               "chirp report task id overlaps ST's CFG_Task_Id_t");

/* Matches the MicroPython demos' 30 s cadence, which the dashboard assumes. */
#define CHIRP_REPORT_INTERVAL_MS 30000u

/* Default UDP hostname and port. A stored host overrides the hostname. */
#define CHIRP_REPORT_DEFAULT_HOST "udp.warbletiot.com"
#define CHIRP_REPORT_UDP_PORT     7701u

static otInstance *s_inst;

/* From the config record. The credential slot holds `token` or `claim`
 * (chirpval enforces at most one). */
static char s_host[64] = CHIRP_REPORT_DEFAULT_HOST;
static char s_token[96];
static char s_hwid[48];
static int  s_configured;

/* Base64 signing key from the config. An empty string disables signing. */
static char s_key_b64[64];

static UTIL_TIMER_Object_t s_tmr;
static int                 s_tmr_made;

/* Allow only one DNS lookup at a time. */
static volatile int s_resolving;

static otUdpSocket s_sock;
static int         s_sock_open;

static unsigned s_ramp = 200u;

/* Narration state: what was last said, so nothing is said twice in a row. */
static int  s_announced;
static char s_last_err[40];

static void say_err(const char *what, const char *detail)
{
  unsigned i = 0u;
  char     key[sizeof s_last_err];

  while (what[i] != '\0' && i < sizeof key - 1u) { key[i] = what[i]; i++; }
  key[i] = '\0';

  for (i = 0u; key[i] != '\0' || s_last_err[i] != '\0'; i++) {
    if (key[i] != s_last_err[i]) { break; }
  }
  if (key[i] == '\0' && s_last_err[i] == '\0') { return; }   /* same as last time */

  for (i = 0u; (s_last_err[i] = key[i]) != '\0'; i++) { }
  board_serial_print("CHIRP. report: %s (%s); retrying every %us\r\n",
       what, detail, CHIRP_REPORT_INTERVAL_MS / 1000u);
}

static void say_ok(unsigned tenths)
{
  if (s_last_err[0] != '\0') {
    s_last_err[0] = '\0';
    board_serial_print("CHIRP. report: recovered\r\n");
  }
  if (!s_announced) {
    s_announced = 1;
    board_serial_print("CHIRP. reporting as %s to %s:%u every %us, %s (%u.%uC sent)\r\n",
         s_hwid, s_host, CHIRP_REPORT_UDP_PORT,
         CHIRP_REPORT_INTERVAL_MS / 1000u,
         (s_key_b64[0] != '\0') ? "signed (L2)" : "token only (L1)",
         tenths / 10u, tenths % 10u);
  }
}

/* UDP has no downlink; otUdpOpen still wants a callback. */
static void udp_rx(void *ctx, otMessage *msg, const otMessageInfo *info)
{
  (void)ctx; (void)msg; (void)info;
}

/* OT context (DNS callback), LL lock held by the caller below. */
static void send_frame(const otIp6Address *addr)
{
  char          buf[256];
  char          sig[CHIRP_SIGN_HEX_LEN + 1];
  const char   *sigp = 0;
  unsigned char payload[CHIRP_FRAME_PAYLOAD_LEN];
  otMessageInfo info;
  otMessage    *msg;
  otError       err;
  unsigned      n, pn, i;
  unsigned      tenths = next_demo_temperature_tenths_c(&s_ramp);

  pn = chirp_frame_payload(payload, sizeof payload, tenths);

  /* Sign with the active key. Skip this frame if signing fails. */
  if (s_key_b64[0] != '\0') {
    if (chirp_sign_hex(sig, sizeof sig, s_key_b64, sizeof s_key_b64, payload, pn) != 0) {
      say_err("signing failed", "stored key is not valid base64");
      return;
    }
    sigp = sig;
  }

  n = chirp_frame_build(buf, sizeof buf, s_hwid, s_token, sigp, payload, pn);
  if (n == 0u) { say_err("frame too long", "hwid/token oversized"); return; }

  if (!s_sock_open) {
    err = otUdpOpen(s_inst, &s_sock, udp_rx, NULL);
    if (err != OT_ERROR_NONE) { say_err("socket open failed", otThreadErrorToString(err)); return; }
    s_sock_open = 1;
  }

  msg = otUdpNewMessage(s_inst, NULL);
  if (msg == NULL) { say_err("no message buffer", "out of buffers"); return; }

  err = otMessageAppend(msg, buf, (uint16_t)n);
  if (err == OT_ERROR_NONE) {
    unsigned char *p = (unsigned char *)&info;
    for (i = 0u; i < sizeof info; i++) { p[i] = 0u; }
    info.mPeerAddr = *addr;
    info.mPeerPort = CHIRP_REPORT_UDP_PORT;
    err = otUdpSend(s_inst, &s_sock, msg, &info);
  }
  if (err != OT_ERROR_NONE) {
    otMessageFree(msg);
    say_err("send failed", otThreadErrorToString(err));
    return;
  }

  say_ok(tenths);
}

static void dns_cb(otError aError, const otDnsAddressResponse *resp, void *ctx)
{
  otIp6Address addr;
  uint32_t     ttl;

  (void)ctx;
  s_resolving = 0;

  if (aError != OT_ERROR_NONE) {
    say_err("dns failed", otThreadErrorToString(aError));
    return;
  }
  if (otDnsAddressResponseGetAddress(resp, 0, &addr, &ttl) != OT_ERROR_NONE) {
    say_err("dns empty", "no address in the answer");
    return;
  }

  LL_LOCK();
  send_frame(&addr);
  LL_UNLOCK();
}

/* Timer-server callback: timer IRQ context, so only set the task. */
static void report_timer_cb(void *arg)
{
  (void)arg;
  UTIL_SEQ_SetTask(CHIRP_TASK_REPORT, CFG_SEQ_PRIO_1);
}

/* Sequencer task CHIRP_TASK_REPORT: one cycle. */
static void chirp_report_task(void)
{
  otDeviceRole role;
  otError      err;

  if (s_inst == NULL || !s_configured) { return; }

  /* Created here so the first run is after MX_APPE_Init(), like the join task. */
  if (!s_tmr_made) {
    if (UTIL_TIMER_Create(&s_tmr, CHIRP_REPORT_INTERVAL_MS, UTIL_TIMER_PERIODIC,
                          report_timer_cb, NULL) != UTIL_TIMER_OK ||
        UTIL_TIMER_Start(&s_tmr) != UTIL_TIMER_OK) {
      board_serial_print("CHIRP. report: no timer available; reporting disabled\r\n");
      return;
    }
    s_tmr_made = 1;
  }

  LL_LOCK();

  role = otThreadGetDeviceRole(s_inst);
  if ((role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER ||
       role == OT_DEVICE_ROLE_LEADER) && !s_resolving) {
    s_resolving = 1;
    err = otDnsClientResolveAddress(s_inst, s_host, dns_cb, NULL, NULL);
    if (err != OT_ERROR_NONE) {
      s_resolving = 0;
      say_err("dns start failed", otThreadErrorToString(err));
    }
  }
  /* Detached: say nothing; the join path narrates the role. */

  LL_UNLOCK();
}

static void copy_str(const char *json, unsigned len, const char *key,
                     char *dst, unsigned dstsz, int *have)
{
  js_val v;

  if (js_obj_get(json, len, key, &v) == 1 && v.t == JS_STR &&
      js_str_copy(&v, dst, dstsz) > 0) {
    if (have != NULL) { *have = 1; }
  }
}

void chirp_report_setup(otInstance *inst)
{
  s_inst = inst;
  (void)snprintf(s_hwid, sizeof s_hwid, "wba65-thread-%06x",
                 (unsigned)(*(volatile const unsigned *)UID_BASE) & 0xFFFFFFu);
  UTIL_SEQ_RegTask(CHIRP_TASK_REPORT, UTIL_SEQ_RFU, chirp_report_task);
}

void chirp_report_config(const char *json, unsigned len)
{
  static const char def[] = CHIRP_REPORT_DEFAULT_HOST;
  unsigned i;

  /* Load the merged config, starting with the default host. */
  for (i = 0u; (s_host[i] = def[i]) != '\0'; i++) { }

  copy_str(json, len, "host", s_host, sizeof s_host, NULL);
  copy_str(json, len, "token", s_token, sizeof s_token, NULL);
  copy_str(json, len, "claim", s_token, sizeof s_token, NULL);
  copy_str(json, len, "hwid",  s_hwid,  sizeof s_hwid,  NULL);

  s_key_b64[0] = '\0';
  copy_str(json, len, "key", s_key_b64, sizeof s_key_b64, NULL);
  if (s_key_b64[0] != '\0') {
    char probe[CHIRP_SIGN_HEX_LEN + 1];
    if (chirp_sign_hex(probe, sizeof probe, s_key_b64, sizeof s_key_b64,
                       (const unsigned char *)"", 0u) != 0) {
      /* An unusable stored key leaves reports unsigned. */
      board_serial_print("CHIRP. stored signing key is unusable; reporting stays unsigned\r\n");
      s_key_b64[0] = '\0';
    }
  }

  s_configured = 1;

  /* Re-announce after any reconfig. */
  s_announced   = 0;
  s_last_err[0] = '\0';

  UTIL_SEQ_SetTask(CHIRP_TASK_REPORT, CFG_SEQ_PRIO_1);
}
