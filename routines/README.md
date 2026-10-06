# Routines: a light in another place, and photos by email

## What happens

These Starlark scripts run on Warblet, not on a board. They connect boards that never talk to each other directly: no hub, and no shared network.

- [pair-lights.star](../olimex-esp32c3-lipo/pair-lights.star): a press on either of two boards turns the other board’s light on. It comes with the Olimex button demo and the XIAO ESP32-C6 demo, and the file is the same in both.
- [doorbell-photo.star](../xiao-esp32s3-sense-cam/doorbell-photo.star): the photo a doorbell camera takes when its own button is pressed is emailed. It comes with the [XIAO ESP32-S3 Sense camera demo](../xiao-esp32s3-sense-cam/README.md#email-a-photo-when-the-button-is-pressed).
- [button-snap.star](button-snap.star) and [camera-photo-alert.star](camera-photo-alert.star): a button press asks a camera for a picture, which is emailed. These two join two different demos, so you set them up yourself.

The first time you set up a demo that comes with a routine from warbletiot.com/flash, Warblet adds the routine to your account, switched on. It is listed on the portal's Routines page, where you can switch it off, edit it or test it with a dry run.

## Two boards, two places

Each board tags the message it sends when its button is pressed `press`. The routine sees the tag, finds the boards in your account that run either demo with `devices_from()`, and sends `led:on` to each of them except the board that spoke. Any board works if its firmware does those two things; the transport does not matter. Two demos in this repository do both: the [Olimex button demo](../olimex-esp32c3-lipo/README.md) (HTTPS) and the [XIAO ESP32-C6 demo](../xiao-esp32c6/README.md) (MQTTS, BOOT button).

### One account for both boards

A routine can only send to devices in its own account. Both boards must be in the account that owns the routine, even when one of them lives in another house.

### Wi-Fi in the other house

The far board needs the other house’s Wi-Fi. The USB setup that every demo answers offers two ways:

- Before you hand the board over, send both networks in its `aps` list (up to eight, tried in order) over the USB setup, as in each board’s manual setup:
  `CHIRP+ {"ssid":"YOUR_WIFI","pass":"YOUR_PASSWORD","aps":[{"ssid":"YOUR_WIFI","pass":"YOUR_PASSWORD"},{"ssid":"THEIR_WIFI","pass":"THEIR_PASSWORD"}]}`
  The top-level `ssid` and `pass` must match the first network in the list.
- At the other house, connect the board to a computer and send only the new network, for example `CHIRP+ {"ssid":"THEIR_WIFI","pass":"THEIR_PASSWORD"}`. A push can carry only the fields that change, so the board keeps its device ID, token and key, and stays in your account. Sending `ssid` without `aps` replaces the stored list with that one network.

The setup page at warbletiot.com/flash also sends Wi-Fi settings, but only when signed in to the account that owns the board.

### Set it up

Set both boards up from warbletiot.com/flash, signed in to the same Warblet account. The first board brings the routine, switched on, as "Pair: a press lights the other board"; the second uses the same one, so your account has one pair routine however many pair boards you set up. Every board in your account that runs either demo is part of the pair: with three boards, a press turns on the other two lights.

Press a button. The other board prints `chirp: down led:on -- LED ON` when it acts on the command.

The routine wakes for every message in your account, because the two boards run different demos, and its minimum interval is 0, so a reading that lands just before a press cannot make it skip the press. Every message adds a row to its run log, which keeps about the last 50, so on an account with other busy devices a press scrolls out of the log within minutes. To test it without pressing, use the editor’s dry run with an event from one board tagged `press`: it queues the light command for the other boards, and nothing for an untagged reading.

To pair boards that run other demos, list those demos in `PAIR_DEMOS`. To pair two particular boards and no others, replace `devices_from(PAIR_DEMOS)` with a list of their two hardware IDs.

### What a press does

A press turns the other board’s light on: the routine sends `led:on` (`LIGHT_COMMAND` in [pair-lights.star](../olimex-esp32c3-lipo/pair-lights.star)). Pressing a board’s own button turns its own light off right after the board tries to send that press, even when the send fails or Wi-Fi is down, so a light can go off for a press the other board never got. So the person who was called answers with a press: their light goes off, and the caller’s light comes on. Pressing again while a light is already on leaves it on.

If the other board is off or out of reach, its commands wait at Warblet for up to 24 hours and arrive when it comes back. With `led:on` that is harmless: the light is simply on. The board still accepts `led:off` and `led:toggle`, so the dashboard’s LED control works as before.

### How long it takes

A board on MQTTS is pushed the command as soon as the routine queues it. A board on HTTPS collects it at its next check: the Olimex and generic demos wait 5 seconds after each check, and a check takes about a second. The Olimex does not look at its button during its one-second command check, so a short press that falls inside it is missed: hold the button for a moment. A command sent while a board is rejoining Wi-Fi waits until it is back. The routine queues a command; nothing reports whether the other board acted on it.

## Camera and email

A button press asks a camera to take a picture. When the camera sends a frame tagged `snap`, a second routine queues an email with that picture attached.

### What you need

- The [Olimex button demo](../olimex-esp32c3-lipo/README.md), with its decoder installed.
- The [MQTTS camera demo](../freenove-wrover-cam-mqtts/README.md), connected to the same Warblet account.
- Each device’s hardware ID and spec, plus email alerts configured for the account.

The [HTTPS camera](../freenove-wrover-cam/README.md) also accepts `snap`, but checks for commands after its scheduled frame rather than receiving a broker push.

### Set up the routines

1. In [button-snap.star](button-snap.star), replace `your-camera-hw-id` in `CAMERA_HW_ID` with your camera’s hardware ID.
2. In [camera-photo-alert.star](camera-photo-alert.star), replace `your-button-hw-id` in `BUTTON_HW_ID` with your Olimex board’s hardware ID.
3. Paste each script into the portal’s routine editor. Scope `button-snap.star` to the button spec and `camera-photo-alert.star` to the camera spec. A spec can contain several devices, so choose the scope to match your setup.
4. Use the editor’s dry run and inspect the queued actions before enabling the routines.
5. Press BUT1 on the Olimex board. Check for a camera frame tagged `snap` and an alert run that queues an attachment.

No firmware is flashed from this directory. Each script defines `routine(event, state, mem)`.

## How the data moves

| Script | Trigger | Action |
|---|---|---|
| `pair-lights.star` | A message tagged `press` from a board running the Olimex or the XIAO ESP32-C6 demo | Sends `led:on` to every other board running either demo |
| `button-snap.star` | A decoded message with `on_press` set | Sends `b"snap"` to the camera |
| `camera-photo-alert.star` | A camera message tagged `snap` | Reads the triggering image and queues an email attachment |
| `doorbell-photo.star` | A doorbell camera message tagged `press` | Reads the triggering image and queues an email attachment |

The Olimex board sends three bytes, a press count and flags; its decoder turns them into `presses`, `button_down` and `on_press`. Only a message sent because of a press has `on_press` set; the timed reports do not. The camera sends a JPEG with a `snap` or `heartbeat` tag. Heartbeat frames do not queue email.

`COOLDOWN_MS = 120000` limits email requests to one per two minutes. Frames inside the cooldown still reach the device page. The cooldown does not guarantee that every queued email is delivered; the account’s alert limits also apply.

## Make it real

Change the device IDs and the `on_press` condition in `button-snap.star` to connect your own sensor and output device.

Change the command bytes to match what your firmware accepts. If you rename a decoded field, update the condition too. In the mail routine, edit `COOLDOWN_MS`, the subject, and the message text as needed.

`state.get()` reads a device’s latest decoded fields. The mailer uses it to include the button’s `presses` count, which the Olimex decoder writes on every message. `mem` stores the last time this routine queued an alert.

`raw_latest(event["hw_id"])` reads the message that triggered the routine. It is called after the cooldown check so skipped emails do not need to load an image.

## Troubleshooting

| Problem | Check |
|---|---|
| No camera command | Check the routine scope, `CAMERA_HW_ID`, and the board’s `on_press` field. |
| Camera command arrives late | The HTTPS camera polls after its scheduled report; the MQTTS camera receives a push. |
| Picture arrives but no email is queued | Check the `snap` tag, cooldown, and routine run log. |
| Press count is zero in email | Check `BUTTON_HW_ID` and that the board’s decoder is installed, so `presses` is a decoded field on that device. |
| Email is queued but not received | Check email setup, alert delivery status, and account limits. |

Routine run logs show printed messages and queued actions. An alert action lists each attachment as `{name, contentType, bytes}`: sizes, never content. Through the API, use `GET /v1/routines/{id}/runs`. A heartbeat run with no actions is expected.
