# Warblet routines — paired lights and photo email

## What happens

These Starlark scripts run on Warblet to connect your devices. Boards can use different networks and transports, but must belong to the same Warblet account.

| Example | Result | Setup |
|---|---|---|
| [pair-lights.star](../olimex-esp32c3-lipo/pair-lights.star) | A button press turns on other paired boards’ lights | Included with the Olimex and XIAO ESP32-C6 demos |
| [doorbell-photo.star](../xiao-esp32s3-sense-cam/doorbell-photo.star) | A doorbell press emails its photo | Included with the XIAO ESP32-S3 Sense camera demo |
| [button-snap.star](button-snap.star) + [camera-photo-alert.star](camera-photo-alert.star) | A separate button requests a camera photo, then emails it | Add both routines manually |

Browser setup adds an included routine, enabled, the first time you install its demo. Use **Routines** in Warblet to edit, test or disable it. No firmware is flashed from this directory.

## Two boards, two places

### Set up paired lights

1. Set up two [Olimex button](../olimex-esp32c3-lipo/README.md) or [XIAO ESP32-C6](../xiao-esp32c6/README.md) demos using [Set up a device](https://warbletiot.com/flash), signed in to the same account.
2. Check that **Pair: a press lights the other board** is enabled on the Routines page. Setup adds one shared routine.
3. Press either button. It turns its own light off and sends a `press` report; the routine sends `led:on` to every other board running either demo in your account.

With three boards, one press lights the other two. Your own light turns off even if the send fails. MQTTS receives pushed commands; HTTPS collects them at its next check, after a five-second wait plus network time. Hold the Olimex button for a moment so the app can see it between requests.

Offline commands wait for reconnection, subject to expiry. A routine run confirms the command was queued, not that the other board acted on it.

### Configure Wi-Fi for another location

The two pair demos accept up to eight networks, tried in order. Send this as one line over USB at 115200 baud, replacing every value:

```text
CHIRP+ {"ssid":"YOUR_WIFI","pass":"YOUR_PASSWORD","aps":[{"ssid":"YOUR_WIFI","pass":"YOUR_PASSWORD"},{"ssid":"THEIR_WIFI","pass":"THEIR_PASSWORD"}]}
```

The top-level network must match the first entry. To replace the network later, send only `ssid` and `pass`; the board keeps its device ID, token and key. Sending `ssid` without `aps` replaces the network list. Browser setup also changes Wi-Fi when signed in to the board’s owning account.

### Customize the pairing

Edit `PAIR_DEMOS` to select other demo groups, or replace `devices_from(PAIR_DEMOS)` with a list of hardware IDs. The firmware must send a `press` tag and accept `LIGHT_COMMAND` (`led:on` by default). Keep the routine scoped to the account with minimum interval 0 so unrelated readings do not suppress a press.

Use a dry run with an event tagged `press` to inspect the proposed commands. A dry run does not send them.

## Camera and email

The [XIAO ESP32-S3 Sense guide](../xiao-esp32s3-sense-cam/README.md#email-a-photo-when-the-button-is-pressed) covers its included doorbell routine. To trigger a separate camera from an Olimex button, use the two scripts below.

### What you need

- The Olimex button demo with its decoder installed.
- The [MQTTS camera demo](../freenove-wrover-cam-mqtts/README.md) in the same account. The [HTTPS camera](../freenove-wrover-cam/README.md) also works but polls for commands after scheduled frames.
- Each device’s hardware ID and spec.
- An alert email address configured in your Warblet account settings.

### Set up the routines

1. In [button-snap.star](button-snap.star), replace `your-camera-hw-id` in `CAMERA_HW_ID` with the camera’s hardware ID.
2. In [camera-photo-alert.star](camera-photo-alert.star), replace `your-button-hw-id` in `BUTTON_HW_ID` with the button board’s hardware ID.
3. Add each script in the routine editor. Scope the button script to the button spec and the email script to the camera spec.
4. Use dry runs to inspect the proposed actions, then enable both routines.
5. Press BUT1. Look for a camera image tagged `snap` and an email action in the routine’s run log.

The button script acts on decoded `on_press`; the email script acts on the camera’s `snap` tag. Scheduled `heartbeat` pictures do not request email. `COOLDOWN_MS = 120000` limits email requests to one every two minutes; account alert limits also apply.

## Make it real

Change the device IDs and the `on_press` condition in `button-snap.star` to connect your own sensor and output device.

Keep command bytes consistent with the target firmware, and update conditions when you rename decoded fields. Edit the email subject, text and `COOLDOWN_MS` for your use case.

Each script defines `routine(event, state, mem)`. `state.get()` reads a device’s latest decoded fields; `mem` keeps values between runs. The email example uses `raw_latest(event["hw_id"])` to load the triggering photo after its cooldown check.

## Troubleshooting

| Problem | Check |
|---|---|
| Paired light stays off | Same account, enabled pair routine, supported demo and `press` tag; inspect the target board’s console. |
| No camera command | Routine scope, `CAMERA_HW_ID` and the decoded `on_press` field. |
| Camera command arrives late | HTTPS cameras poll; MQTTS cameras receive a push. |
| Photo arrives but no email action | The expected tag (`snap` for the separate camera, `press` for the doorbell), cooldown and run log. |
| Press count is zero in email | `BUTTON_HW_ID` and the button decoder’s `presses` field. |
| Email queued but not received | Alert email address, delivery status and account limits. |

Run logs show printed messages and queued actions. Attachment metadata has the shape `{name, contentType, bytes}`. API clients can read logs with `GET /v1/routines/{id}/runs`; a heartbeat run with no actions is expected.
