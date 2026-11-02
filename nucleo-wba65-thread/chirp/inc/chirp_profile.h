/* What this board takes in a setup push, and what it says it can do.
 * chirpval.c and json.c are the same files in both Nucleo demos; this header,
 * under the same name in each, is where the two demos differ. */
#ifndef CHIRP_PROFILE_H
#define CHIRP_PROFILE_H

/* The longest id this board holds (CHIRP-PROV v1.8: at least 31). A pushed id
 * that is longer is refused, never cut short. */
#define CHIRP_HWID_LEN_MAX 47u

/* A Thread board has no Wi-Fi: ssid, pass and aps are refused. Its settings
 * are complete with a credential and a thread object; the id is its own until
 * one is pushed. */
#define CHIRP_TAKES_WIFI   0
#define CHIRP_NEEDS_THREAD 1

/* The capability object the announce carries (caps=), exactly demo.json's
 * firmware.capabilities: token and signed, a key over USB, reports over UDP. */
#define CHIRP_CAPS "{\"v\":1,\"auth\":[1,2],\"keyProvisioning\":[\"usb\"]," \
                   "\"otaKeyRotation\":false,\"otaFirmwareUpdate\":false,\"transports\":[\"udp\"]}"

#endif /* CHIRP_PROFILE_H */
