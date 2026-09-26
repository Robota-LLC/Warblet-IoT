# Freenove ESP32-WROVER CAM — pictures on command over MQTTS

## What happens

The camera keeps an MQTT-over-TLS connection to Warblet. A `snap` command asks it to capture and send a JPEG tagged `snap`. It also sends a 320 × 240 picture every 300 seconds, tagged `heartbeat`.

## What you need

- A Freenove ESP32-WROVER CAM with 4 MB flash and 8 MB PSRAM.
- An OV2640 or GC0308 camera module seated in the ribbon socket.
- A USB data cable for the board’s CH340 serial connection.
- A 2.4 GHz Wi-Fi network and a Warblet account.
- ESP-IDF v5.5 on your computer.

## Camera pins and settings

The ribbon socket provides all camera connections. The pin constants are at the top of `main/camera.c`:

| Signal | GPIO |
|---|---|
| XCLK | 21 |
| SDA / SCL | 26 / 27 |
| D0–D7 | 4, 5, 18, 19, 36, 39, 34, 35 |
| VSYNC / HREF / PCLK | 25 / 23 / 22 |
| PWDN / RESET | Not connected (`-1`) |

GPIO4 carries camera data; it is not a flash LED. The app requires PSRAM. It uses hardware JPEG output when supported, or captures RGB565 and encodes it in software. `CAM_HMIRROR` controls a horizontal flip on the software path.

`SENSOR_JPEG_QUALITY` and `SOFTWARE_JPEG_QUALITY` control compression. Frame size is set to `FRAMESIZE_QVGA` in `camera_init()`. Capture checks frame age and tries up to three times; it can return an older frame after that limit. Keep the transmitted message within 256 KiB.

## Build

Open an ESP-IDF v5.5 terminal, then change to this demo’s directory.

```sh
idf.py set-target esp32
idf.py build
idf.py merge-bin
```

The camera component version is pinned in `main/idf_component.yml`; the build downloads it. Keep the PSRAM and certificate settings in `sdkconfig.defaults`.

## Flash

Replace `<PORT>` with the board’s serial port, such as `COM5` or `/dev/ttyUSB0`. For an initial install:

```sh
idf.py -p <PORT> flash monitor
```

For a board with the matching bootloader and partition table already installed, write only the app to preserve its stored settings:

```sh
esptool --chip esp32 --port <PORT> write_flash 0x10000 build/freenove-wrover-cam-mqtts-demo.bin
```

You can also select the image in https://warbletiot.com/flash. Use `build/merged-binary.bin` at `0x0` for an initial install, or `build/freenove-wrover-cam-mqtts-demo.bin` at `0x10000` for an app update. A merged-image write can erase stored credentials.

## Set up the connection

1. Open https://warbletiot.com/flash in Chrome or Edge and connect the board’s serial port.
2. Choose your Wi-Fi network and Warblet device, then send the settings.
3. Open the device page to see incoming messages. Close other serial tools while the browser uses the port.

For manual setup, use a serial terminal at 115200 baud, 8 data bits, no parity, and 1 stop bit. Replace these values and send each command on one line:

```text
CHIRP?
CHIRP+ {"ssid":"YOUR_WIFI","pass":"YOUR_WIFI_PASSWORD","hwid":"YOUR_DEVICE_ID","token":"YOUR_DEVICE_TOKEN"}
```

The board saves settings in NVS flash and restarts. Keep settings and keys private. A later push can contain only changed fields. An optional `key` holds a base64-encoded 32-byte signing key. Use either `claim` or `token`, not both. A push with any malformed field, such as a key that does not decode to 32 bytes or a network without an `ssid`, is refused whole with `CHIRP= err bad-json` and nothing is saved.

An `aps` list can hold up to eight networks. The demo tries visible networks from strongest to weakest signal, then any remaining networks in saved order. Sending `ssid` without `aps` replaces the list with one network. A stored `host` overrides the default `mqtt.warbletiot.com`.

## What goes over the wire

The board connects to `mqtt.warbletiot.com:8883` using TLS and the ESP-IDF certificate bundle. This sender has no plaintext setting. The device ID is both the client ID and username; the token or claim code is the password.

| Direction | Topic | Data |
|---|---|---|
| Scheduled image | `chirp/{hwid}/up/heartbeat` | JPEG frame |
| Requested image | `chirp/{hwid}/up/snap` | JPEG frame |
| Command | `chirp/{hwid}/down` | `snap` |

With a signing key, the published bytes are a 32-byte raw HMAC-SHA256 followed by the JPEG. The signed input is `"tag=<tag>\n" + JPEG`. The tag line is included in the signature calculation, not inserted into the frame. The platform verifies and removes the signature before storing the JPEG.

These messages have no nonce and no replay protection. They use QoS 0, which has no delivery acknowledgement. Confirm receipt on the device page. The 256 KiB message limit includes the 32 signature bytes when present.

Set a dashboard button’s text to `snap`. The command handler logs and ignores other commands. Signing keys arrive through USB. The [routine examples](../routines/README.md) can request a frame when a button demo reports a press and email the result.

## Decoder

The JPEG can be displayed without a decoder. The optional [decoder.star](decoder.star) returns `bytes`, the stored frame size, alongside the picture. Raw images are available through `/v1/devices/{id}/raw/latest`.

## Make it real

Replace `camera_init()`, `camera_capture()`, and `camera_release()` in `main/camera.c` with your sensor’s setup, read, and cleanup functions.

Keep the interface in `main/camera.h`: capture returns a byte pointer and length, valid until release. `send_camera_frame()` publishes the data before releasing it. For non-image data, replace the JPEG check and update `decoder.star`. Set the interval with `REPORT_PERIOD_MS` in `main/chirp_mqtt.c`.

## Security notes

The device credential, the signing key, and the Wi-Fi passwords are stored unencrypted in the NVS partition. Anyone who can reach the USB port can read them with `esptool read_flash`, replace them with a `CHIRP+` push, or reflash the board: physical access is full access. For a product, turn on flash encryption with NVS encryption and secure boot, lock down the provisioning console, and give every device its own token and key. The MQTTS connection checks the broker’s certificate against the ESP-IDF certificate bundle and the host name.

## Troubleshooting

| Problem | Check |
|---|---|
| No networks stored | Send Wi-Fi settings and a device credential. |
| JPEG format unsupported at startup | The driver tries RGB565 and software JPEG; check whether the next camera initialization succeeds. |
| Camera probe or capture fails | Power off, check the ribbon seating, then restart. Check PSRAM settings too. |
| Picture is mirrored | Check `CAM_HMIRROR` for software-encoded frames. |
| Frame-age message | Capture is discarding an old queued frame. Repeated stale results can mean the sensor is delivering slowly. |
| Message over the byte cap | Reduce frame dimensions or JPEG size. |
| MQTT connection fails | Check Wi-Fi, DNS, the MQTT hostname, and the credential. |
| Publishes work but commands do not | Look for a successful subscription in the console; check the device ID and `snap` control text. |
| No image on the device page | Check its rejected-message count and signing key. QoS 0 does not report rejected payloads to the board. |
| No room for an envelope | Check that PSRAM is enabled and that the frame plus signature fits in memory. |
