# Seeed Studio XIAO ESP32-S3 Sense — photo doorbell over MQTTS

## What happens

Press BOOT on the board and it takes a photo and sends it to Warblet, tagged `press`. A switch wired from pin D0 to GND does the same, so a doorbell button or a door contact can take the picture. The board also takes a photo when Warblet sends it the `snap` command, and sends one every 300 s tagged `heartbeat`. The first time you set this demo up from warbletiot.com/flash, Warblet also adds the [doorbell routine](doorbell-photo.star) to your account, switched on: set an alert email address in your account's settings, and a press emails you that photo.

## What you need

- A Seeed Studio XIAO ESP32-S3 Sense: the XIAO ESP32-S3 with its camera board attached. It has 8 MB of flash and 8 MB of PSRAM.
- The supplied Wi-Fi antenna, fitted to its connector. See [Seeed’s setup guide](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/).
- A USB-C data cable.
- A 2.4 GHz Wi-Fi network and a Warblet account.
- An alert email address in your Warblet account’s settings, for photo email.
- ESP-IDF v5.5, to build from source.
- Optional: a push button or door contact and two wires.

## Quick start

Open [Set up a device](https://warbletiot.com/flash) in desktop Chrome or Edge, connect the board and select this demo. Follow the prompts to install it and send your network settings. Open the device page to see the result.

To customize the firmware first, use the build and flash steps below.

## The button

| Input | Pin | What you wire |
|---|---|---|
| BOOT button | GPIO0 | Nothing: it is on the board. |
| Your button or door contact | D0 (GPIO1) | A switch between D0 and GND. |

Both inputs use pull-ups and trigger when connected to ground. Holding or releasing a button takes no additional photo. For a door, use a contact that closes when the door opens; a held D0 input does not prevent BOOT from working.

The board takes at most one press photo every 30 seconds (`PRESS_GAP_MS` in `main/main.c`). Presses during that interval are ignored.

Use D0 (GPIO1) for the external switch. Avoid strapping pins GPIO0, GPIO3, GPIO45 and GPIO46 when moving it. Press BOOT only after startup; holding it during reset enters the bootloader.

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

The demo detects OV2640 and OV3660 sensors at startup. For OV3660, `camera_init()` applies a vertical flip, brightness +1 and saturation −2. Adjust those settings for your scene.

`FRAME_SIZE` and `JPEG_QUALITY` in `main/camera.c` set the picture size (640 × 480) and compression. Keep the message within 256 KiB.

`camera_capture()` discards frames older than `FRAME_STALE_MS` (2000 ms), with up to three capture attempts. Repeated frame-age warnings are covered in Troubleshooting.

## Build by hand

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

The board saves settings in NVS flash and restarts. Later pushes can contain only changed fields. The optional `key` is a base64-encoded 32-byte signing key; use either `claim` or `token`, not both. Invalid settings are refused without saving.

An `aps` list can hold up to eight networks. The demo tries visible networks from strongest to weakest signal, then any remaining networks in saved order. Sending `ssid` without `aps` replaces the list with one network. A stored `host` overrides the default `mqtt.warbletiot.com`.

## Data and commands

| Field | Value |
|---|---|
| **Transport** | MQTT over TLS to `mqtt.warbletiot.com:8883` |
| **Security cap** | L2 (Signed), with a key; L1 (Token) without one |
| **Announce** | `CHIRP! v=1 hw=cam-a1b2c3 radios=wifi sig=1 t=demo-xiao-s3-cam` |

The board verifies the broker with the ESP-IDF certificate bundle. The device ID is both the MQTT client ID and username; the token or claim code is the password. This sender has no plaintext setting.

| Direction | Topic | Data |
|---|---|---|
| Button photo | `chirp/{hwId}/up/press` | JPEG frame |
| Requested photo | `chirp/{hwId}/up/snap` | JPEG frame |
| Scheduled photo | `chirp/{hwId}/up/heartbeat` | JPEG frame |
| Command | `chirp/{hwId}/down` | `snap` |

The topic tag identifies the photo as `press`, `snap` or `heartbeat`; routines read it as `event["tag"]`. The payload is a JPEG.

With a signing key, the published bytes are a 32-byte raw HMAC-SHA256 followed by the JPEG. The signed input is `"\ntag=<tag>\n" + JPEG`: an empty nonce line, because MQTT carries no nonce, then the tag line. Both lines are included in the signature calculation, not inserted into the frame. The platform verifies and removes the signature before storing the JPEG.

These messages have no nonce and no replay protection. They use QoS 0, which has no delivery acknowledgement. Confirm receipt on the device page. The 256 KiB message limit includes the 32 signature bytes when present.

## Email a photo when the button is pressed

Browser setup adds [doorbell-photo.star](doorbell-photo.star) as **Doorbell: email the photo**, enabled on this demo’s spec. Set an alert email address in your account settings. Manage or disable the routine on the **Routines** page.

For manual setup, paste the script into the routine editor, select this board’s spec and set the minimum interval to 0. Only photos tagged `press` request email; `snap` and `heartbeat` photos do not. The routine’s six-minute cooldown limits email requests; photos still appear on the device page during that time, and account alert limits also apply.

A photo request waiting more than 10 seconds on the board is dropped (`REQUEST_MAX_AGE_MS`). A pending press keeps its `press` tag if a `snap` arrives before capture. To trigger this camera from another board, see the [routine examples](../routines/README.md).

## Decoder

The JPEG can be displayed without a decoder. The optional [decoder.star](decoder.star) returns `bytes`, the stored frame size, alongside the picture. Raw images are available through `/v1/devices/{id}/raw/latest`.

## Make it real

Change what happens on a press in `button_task()` in `main/main.c`: it is a plain loop that reads the two pins every 10 ms and calls `chirp_mqtt_request(CHIRP_TAG_PRESS)` once per press.

To photograph a door both when it opens and when it shuts, add a second tag for the other edge in `button_task()` and a routine that keys on it. To use another camera, replace `camera_init()`, `camera_capture()` and `camera_release()` in `main/camera.c`, keeping the interface in `main/camera.h`. Set the heartbeat interval with `REPORT_PERIOD_MS` in `main/chirp_mqtt.c`.

## Security notes

The MQTTS connection verifies the server certificate and hostname using the ESP-IDF certificate bundle.

NVS stores Wi-Fi passwords, the device token and signing key unencrypted. USB access allows reading or replacing them. For deployment, use separate device credentials and configure flash/NVS encryption, secure boot and console access for your hardware.

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
