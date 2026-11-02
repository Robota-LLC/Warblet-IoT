/* Telemetry loop: one CHIRP1 UDP datagram every 30 s to the configured host,
 * resolved through the border router's DNS. */
#ifndef CHIRP_REPORT_H
#define CHIRP_REPORT_H

#include "openthread/instance.h"

/* Register the sequencer task. Called once, from __wrap_otCliInit, next to
 * the other RegTask calls. The id comes from chirp_hwid.c. */
void chirp_report_setup(otInstance *inst);

/* Load host, identity, credential, and signing key from validated config JSON.
 * Schedule reports with these settings; with no credential, nothing is sent. */
void chirp_report_config(const char *json, unsigned len);

/* 1 when a signing key that will sign is held (the announce's key_set=). */
int chirp_report_key_set(void);

#endif /* CHIRP_REPORT_H */
