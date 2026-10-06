# Seeed Studio XIAO ESP32-S3 Sense — photo doorbell over MQTTS

## What happens

Press BOOT on the board and it takes a photo and sends it to Warblet, tagged `press`. A switch wired from pin D0 to GND does the same, so a doorbell button or a door contact can take the picture. The board also takes a photo when Warblet sends it the `snap` command, and sends one every 300 s tagged `heartbeat`. The first time you set this demo up from warbletiot.com/flash, Warblet also adds the [doorbell routine](doorbell-photo.star) to your account, switched on: set an alert email address in your account's settings, and a press emails you that photo.

## What you need

- A Seeed Studio XIAO ESP32-S3 Sense: the XIAO ESP32-S3 with its camera board attached. It has 8 MB of flash and 8 MB of PSRAM.
- The Wi-Fi antenna from the box, fitted to the board's antenna connector. Seeed's [getting-started page](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) shows how: hook one side of the plug into the connector first, then press the other side down.
- A USB-C data cable.
- A 2.4 GHz Wi-Fi network and a Warblet account.
- For the email: an alert email address set in your Warblet account's settings. Alert email is off until you set one.
- ESP-IDF v5.5 on your computer.
- Optional: a push button or a door contact and two wires, if you want a button away from the board.

## The button

| Input | Pin | What you wire |
|---|---|---|
| BOOT button | GPIO0 | Nothing: it is on the board. |
| Your button or door contact | D0 (GPIO1) | A switch between D0 and GND. |

Both inputs read low while pressed, and either one takes the photo. Each is checked on its own, so a door contact held closed on D0 does not stop BOOT from working. The chip's own pull-up holds D0 high while the switch is open, so an unwired D0 does nothing. The photo is taken when the switch closes: holding it does nothing more, and neither does letting go. For a door, use a contact that closes when the door opens.

The board takes at most one press photo every 30 seconds (`PRESS_GAP_MS` in `main/main.c`). A press inside that time is ignored and logged on the console. This keeps a rattling contact from sending photo after photo and using up the device's daily storage.

Seeed's [getting-started page](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) lists GPIO0, GPIO3, GPIO45 and GPIO46 as strapping pins: the chip reads them at reset to decide how to start. That is why the switch goes on D0 and not on D2, which is GPIO3. BOOT is GPIO0 too, but its only effect at reset is the one it is designed for: hold it while the board starts and the board waits to be flashed. Press it once the board is running.

The pins are set at the top of `main/main.c`: `BUTTON_BOOT_GPIO` and `BUTTON_D0_GPIO`.

## Camera pins and settings

The camera board plugs into the XIAO's connector, so there is nothing to wire. The pin map is from the camera slot table on Seeed's [camera usage page](https://wiki.seeedstudio.com/xiao_esp32s3_camera_usage/); the constants are at the top of `main/camera.c`.

| Signal | GPIO |
|---|---|
| XCLK | 10 |
| SDA / SCL | 40 / 39 |
| D0–D7 (Y2–Y9) | 15, 17, 18, 16, 14, 12, 11, 48 |
| VSYNC / HREF / PCLK | 38 / 47 / 13 |
| PWDN / RESET | Not connected (`-1`) |

Seeed fitted an OV2640 to earlier boards and fits an OV3660 now. The demo does not assume either: it asks the sensor at start-up and prints its name, for example `camera: sensor OV3660 (PID 0x3660) up, 640x480 JPEG`. Both sensors encode the JPEG themselves. On an OV3660 the demo applies the settings Seeed's examples use for that sensor: a vertical flip, brightness +1 and saturation −2.

`FRAME_SIZE` and `JPEG_QUALITY` in `main/camera.c` set the picture size (640 × 480) and compression. Keep the message within 256 KiB.

## Why the first frame after idle is old

The camera driver keeps filling its one frame buffer and then waits, so the frame waiting in the buffer may have been taken minutes ago. `camera_capture()` checks the frame's age against `FRAME_STALE_MS` (2000 ms) and takes a new one if it is older, up to three tries. The age is measured on the chip's own timer (`esp_timer_get_time()`), not the wall clock. Without this check, the photo of a press would be the scene from the previous heartbeat.

## Build

Open an ESP-IDF v5.5 terminal, then change to this demo's directory.

```sh
idf.py set-target esp32s3
idf.py build
idf.py merge-bin -o xiao-esp32s3-sense-cam-full.bin
```

The camera component version is pinned in `main/idf_component.yml`; the build downloads it. Keep the PSRAM, USB console and certificate settings in `sdkconfig.defaults`.

## Flash

The board's USB-C port is the ESP32-S3's own USB, so there is no serial bridge chip. Replace `<PORT>` with the board's serial port, such as `COM5` or `/dev/ttyACM0`. For an initial install:

```sh
idf.py -p <PORT> flash monitor
```

For a board with the matching bootloader and partition table already installed, write only the app to preserve its stored settings:

```sh
esptool --chip esp32s3 --port <PORT> --baud 115200 write_flash 0x10000 build/xiao-esp32s3-sense-cam-update.bin
```

You can also select the image in https://warbletiot.com/flash. Use `build/xiao-esp32s3-sense-cam-full.bin` at `0x0` for an initial install, or `build/xiao-esp32s3-sense-cam-update.bin` at `0x10000` for an app update. A merged-image write can erase stored credentials. If the port does not appear, hold BOOT while you plug the cable in, then flash.

## Set up the connection

1. Open https://warbletiot.com/flash in Chrome or Edge and connect the board's serial port.
2. Choose your Wi-Fi network and Warblet device, then send the settings.
3. Open the device page to see incoming messages. Close other serial tools while the browser uses the port.

For manual setup, use a serial terminal on the board's USB port. The board speaks CHIRP-PROV v1.6 there. Replace these values and send each command on one line:

```text
CHIRP?
CHIRP+ {"ssid":"YOUR_WIFI","pass":"YOUR_WIFI_PASSWORD","hwid":"YOUR_DEVICE_ID","token":"YOUR_DEVICE_TOKEN"}
```

The board answers `CHIRP?` with a line like this:

```text
CHIRP! v=1 hw=cam-a1b2c3 radios=wifi sig=1 t=demo-xiao-s3-cam
```

The board saves settings in NVS flash and restarts. Keep settings and keys private. A later push can contain only changed fields. An optional `key` holds a base64-encoded 32-byte signing key. Use either `claim` or `token`, not both. A push with any malformed field, such as a key that does not decode to 32 bytes or a network without an `ssid`, is refused whole with `CHIRP= err bad-json` and nothing is saved. A push that leaves the board with no network or no credential is refused with `CHIRP= err missing-field`.

An `aps` list can hold up to eight networks. The demo tries visible networks from strongest to weakest signal, then any remaining networks in saved order. Sending `ssid` without `aps` replaces the list with one network. A stored `host` overrides the default `mqtt.warbletiot.com`.

## What goes over the wire

| Field | Value |
|---|---|
| **Transport** | MQTT over TLS to `mqtt.warbletiot.com:8883` |
| **Security cap** | L2 (Signed), with a key; L1 (Token) without one |
| **Announce** | `CHIRP! v=1 hw=cam-a1b2c3 radios=wifi sig=1 t=demo-xiao-s3-cam` |

The board connects using TLS and the ESP-IDF certificate bundle. This sender has no plaintext setting. The device ID is both the client ID and username; the token or claim code is the password. The board subscribes to its command topic after every connection, whatever the broker's `session_present` says.

| Direction | Topic | Data |
|---|---|---|
| Button photo | `chirp/{hwId}/up/press` | JPEG frame |
| Requested photo | `chirp/{hwId}/up/snap` | JPEG frame |
| Scheduled photo | `chirp/{hwId}/up/heartbeat` | JPEG frame |
| Command | `chirp/{hwId}/down` | `snap` |

Every photo goes to `chirp/{hwId}/up/<tag>`: the tag says why the photo was taken, and a routine reads it as `event["tag"]`. Each payload is the JPEG itself, starting with the bytes `FF D8 FF`. The MQTT output buffer is 32768 bytes (`MQTT_OUT_BUFFER` in `main/chirp_mqtt.c`).

With a signing key, the published bytes are a 32-byte raw HMAC-SHA256 followed by the JPEG. The signed input is `"\ntag=<tag>\n" + JPEG`: an empty nonce line, because MQTT carries no nonce, then the tag line. Both lines are included in the signature calculation, not inserted into the frame. The platform verifies and removes the signature before storing the JPEG.

These messages have no nonce and no replay protection. They use QoS 0, which has no delivery acknowledgement. Confirm receipt on the device page. The 256 KiB message limit includes the 32 signature bytes when present.

## Email a photo when the button is pressed

[`doorbell-photo.star`](doorbell-photo.star) runs on Warblet, not on the board. The first time you set this demo up from warbletiot.com/flash, Warblet adds it to your account as the routine "Doorbell: email the photo", switched on, triggered by this demo's spec, with a minimum interval of 0 so a heartbeat just before a press cannot make it skip the press. It is listed on the portal's Routines page, where you can switch it off, edit it or test it with a dry run.

Set an alert email address in your account's settings. Alert email is off until you do.

If you set the board up another way, paste the script into the portal's routine editor, choose this board's spec as its trigger, and set the minimum interval to 0.

For each photo tagged `press`, the routine reads that photo with `raw_latest(event["hw_id"])` and queues an alert email with the photo attached. Heartbeat and `snap` photos are not mailed. The account sends at most 10 alert emails an hour, so the routine waits six minutes after an email before it sends another (`COOLDOWN_MS`). Presses inside that time still reach the device page.

The board answers a press or a `snap` only while the photo can still show that moment: a request it cannot photograph within 10 seconds, for example because the board is reconnecting, is dropped and logged (`REQUEST_MAX_AGE_MS` in `main/chirp_mqtt.c`). If a `snap` arrives while a press is still waiting for its photo, the photo keeps the `press` tag, so it is still mailed.

A button on another board can take a photo too: a routine on that board's spec sends `b"snap"` to this camera. See the [routine examples](../routines/README.md).

## Decoder

The JPEG can be displayed without a decoder. The optional [decoder.star](decoder.star) returns `bytes`, the stored frame size, alongside the picture. Raw images are available through `/v1/devices/{id}/raw/latest`.

## Make it real

Change what happens on a press in `button_task()` in `main/main.c`: it is a plain loop that reads the two pins every 10 ms and calls `chirp_mqtt_request(CHIRP_TAG_PRESS)` once per press.

To photograph a door both when it opens and when it shuts, add a second tag for the other edge in `button_task()` and a routine that keys on it. To use another camera, replace `camera_init()`, `camera_capture()` and `camera_release()` in `main/camera.c`, keeping the interface in `main/camera.h`. Set the heartbeat interval with `REPORT_PERIOD_MS` in `main/chirp_mqtt.c`.

## Security notes

The device credential, the signing key, and the Wi-Fi passwords are stored unencrypted in the NVS partition. Anyone who can reach the USB port can read them with `esptool read_flash`, replace them with a `CHIRP+` push, or reflash the board: physical access is full access. For a product, turn on flash encryption with NVS encryption and secure boot, lock down the provisioning console, and give every device its own token and key. The MQTTS connection checks the broker's certificate against the ESP-IDF certificate bundle and the host name.

## Troubleshooting

| Problem | Check |
|---|---|
| No networks stored | Send Wi-Fi settings and a device credential. |
| Board does not join Wi-Fi | The ESP32-S3 uses 2.4 GHz only. Check the antenna is fitted. |
| Console says `no camera` once a minute | The board sends nothing without a camera but stays reachable over USB. Unplug it and check the camera board sits fully on its connector. To take the boards apart, slide them sideways as Seeed's getting-started page shows; never pry them apart up and down, which can break the connector. |
| Pressing BOOT does nothing | Look for `button pressed (GPIO0)` on the console. Press after the board has started, not while plugging it in. A press within 30 s of the last photo is ignored. |
| D0 switch does nothing | Check the wire goes to D0 and GND, and that the switch closes. |
| Photo is upside down | `set_vflip()` in `camera_init()` flips the OV3660 picture; an OV2640 is left as it is. |
| Frame-age message | Capture is discarding an old queued frame. Repeated stale results can mean the sensor is delivering slowly. |
| MQTT connection fails | Check Wi-Fi, DNS, the MQTT hostname, and the credential. |
| Commands do not arrive | Look for a successful subscription on the console; check the device ID and `snap` control text. |
| No email after a press | Check the account's alert email address, the routine's scope, its minimum interval, the cooldown, and its run log. |
| No image on the device page | Check its rejected-message count and signing key. QoS 0 does not report rejected payloads to the board. |
