# Button, camera, and email routines

## What happens

A button press asks a camera to take a picture. When the camera sends a frame tagged `snap`, a second routine queues an email with that picture attached. These Starlark scripts run on Warblet, not on either board.

## What you need

- The [Olimex button demo](../olimex-esp32c3-lipo/README.md), with its decoder installed.
- The [MQTTS camera demo](../freenove-wrover-cam-mqtts/README.md), connected to the same Warblet account.
- Each device’s hardware ID and spec, plus email alerts configured for the account.

The [HTTPS camera](../freenove-wrover-cam/README.md) also accepts `snap`, but checks for commands after its scheduled frame rather than receiving a broker push.

## Set up the routines

1. In [button-snap.star](button-snap.star), replace `your-camera-hw-id` in `CAMERA_HW_ID` with your camera’s hardware ID.
2. In [camera-photo-alert.star](camera-photo-alert.star), replace `your-button-hw-id` in `BUTTON_HW_ID` with your Olimex board’s hardware ID.
3. Paste each script into the portal’s routine editor. Scope `button-snap.star` to the button spec and `camera-photo-alert.star` to the camera spec. A spec can contain several devices, so choose the scope to match your setup.
4. Use the editor’s dry run and inspect the queued actions before enabling the routines.
5. Press BUT1 on the Olimex board. Check for a camera frame tagged `snap` and an alert run that queues an attachment.

No firmware is flashed from this directory. Each script defines `routine(event, state, mem)`.

## How the data moves

| Script | Trigger | Action |
|---|---|---|
| `button-snap.star` | A decoded message with `on_press` set | Keeps the board’s `presses` count as the triggering device’s tracked `presses` and sends `b"snap"` to the camera |
| `camera-photo-alert.star` | A camera message tagged `snap` | Reads the triggering image and queues an email attachment |

The Olimex board sends three bytes, a press count and flags; its decoder turns them into `presses`, `button_down` and `on_press`. Only a message sent because of a press has `on_press` set; the timed reports do not. The camera sends a JPEG with a `snap` or `heartbeat` tag. Heartbeat frames do not queue email.

`COOLDOWN_MS = 120000` limits email requests to one per two minutes. Frames inside the cooldown still reach the device page. The cooldown does not guarantee that every queued email is delivered; the account’s alert limits also apply.

## Make it real

Change the device IDs and the `on_press` condition in `button-snap.star` to connect your own sensor and output device.

Change the command bytes to match what your firmware accepts. If you rename a decoded field, update the condition too. In the mail routine, edit `COOLDOWN_MS`, the subject, and the message text as needed.

`track()` saves a value on the triggering device. `state.get()` reads a device’s saved state. The mailer uses this to include the button’s press count. `mem` stores the last time this routine queued an alert.

`raw_latest(event["hw_id"])` reads the message that triggered the routine. It is called after the cooldown check so skipped emails do not need to load an image.

## Troubleshooting

| Problem | Check |
|---|---|
| No camera command | Check the routine scope, `CAMERA_HW_ID`, and the board’s `on_press` field. |
| Camera command arrives late | The HTTPS camera polls after its scheduled report; the MQTTS camera receives a push. |
| Picture arrives but no email is queued | Check the `snap` tag, cooldown, and routine run log. |
| Press count is zero in email | Check `BUTTON_HW_ID` and whether the button routine tracks `presses` on that device. |
| Email is queued but not received | Check email setup, alert delivery status, and account limits. |

Routine run logs show printed messages and queued actions. Through the API, use `GET /v1/routines/{id}/runs`. A heartbeat run with no actions is expected.
