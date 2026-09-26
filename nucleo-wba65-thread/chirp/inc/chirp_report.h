/* Telemetry loop: one CHIRP1 UDP datagram every 30 s to the configured host,
 * resolved through the border router's DNS. */
#ifndef CHIRP_REPORT_H
#define CHIRP_REPORT_H

#include "openthread/instance.h"

/* Register the sequencer task and learn the die's default hardware id.
 * Called once, from __wrap_otCliInit, next to the other RegTask calls. */
void chirp_report_setup(otInstance *inst);

/* Load host, identity, credential, and signing key from validated config JSON.
 * Schedule reports with these settings. */
void chirp_report_config(const char *json, unsigned len);

#endif /* CHIRP_REPORT_H */
