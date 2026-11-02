/* What this board takes in a setup push, and what it says it can do.
 * chirpval.c and json.c are the same files in both Nucleo demos; this header,
 * under the same name in each, is where the two demos differ. */
#ifndef CHIRP_PROFILE_H
#define CHIRP_PROFILE_H

/* The longest id this board holds (CHIRP-PROV v1.8: at least 31). A pushed id
 * that is longer is refused, never cut short. */
#define CHIRP_HWID_LEN_MAX 63u

/* No radio: this board reports through the computer it is plugged into. It
 * still takes the Wi-Fi and Thread fields and keeps them unused, because the
 * setup page sends them to a board that announces no radio. A credential alone
 * is complete; the id is its own until one is pushed. */
#define CHIRP_TAKES_WIFI   1
#define CHIRP_NEEDS_THREAD 0

/* The capability object the announce carries (caps=), exactly demo.json's
 * firmware.capabilities: token and signed, a key over USB, and http, the
 * computer relay's route to Warblet. */
#define CHIRP_CAPS "{\"v\":1,\"auth\":[1,2],\"keyProvisioning\":[\"usb\"]," \
                   "\"otaKeyRotation\":false,\"otaFirmwareUpdate\":false,\"transports\":[\"http\"]}"

#endif /* CHIRP_PROFILE_H */
