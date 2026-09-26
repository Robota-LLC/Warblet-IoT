# ESP32 — chip temperature over HTTPS

## What happens

The board sends its chip temperature to Warblet every 30 seconds over HTTPS. An optional LED flashes when the server accepts a message. If the temperature API is unavailable, the app skips the reading.

## What you need

- An ESP32, ESP32-S3, ESP32-C3, or ESP32-C6 board.
- A USB data cable.
- A 2.4 GHz Wi-Fi network and a Warblet account.

## Pins and settings

Check your board’s pinout before running the app. `LED_PIN = 2` is a default, not a detected LED. Set it to your LED’s GPIO, or to `None` to disable it. Set `LED_ACTIVE_LOW` to match the wiring. No external sensor is needed.

`INTERVAL_S` in `main.py` sets the reporting interval. `read_temperature_c()` tries the Celsius API, then converts the ESP32 Fahrenheit API to Celsius. Die temperature measures the chip, not the room.

## Build

The app runs as MicroPython source. Install the computer tools:

```sh
python -m pip install esptool mpremote
```

## Flash

Run from this demo’s directory. Replace `<PORT>` with the board’s serial port, such as `COM5` or `/dev/ttyACM0`.

Use the MicroPython 1.28.0 image for your chip. [demo.json](demo.json) also lists its SHA-256 checksum. Erasing flash removes stored files and settings.

| Chip | Download | Offset |
|---|---|---|
| ESP32 | [ESP32_GENERIC-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC-20260406-v1.28.0.bin) | `0x1000` |
| ESP32-S3 | [ESP32_GENERIC_S3-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_S3-20260406-v1.28.0.bin) | `0x0` |
| ESP32-C3 | [ESP32_GENERIC_C3-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_C3-20260406-v1.28.0.bin) | `0x0` |
| ESP32-C6 | [ESP32_GENERIC_C6-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_C6-20260406-v1.28.0.bin) | `0x0` |

These commands are for ESP32-C3. For another chip, change the chip name, filename, and offset together.

```sh
esptool --chip esp32c3 --port <PORT> erase_flash
esptool --chip esp32c3 --port <PORT> write_flash 0x0 ESP32_GENERIC_C3-20260406-v1.28.0.bin
```
Copy the files, then press RESET:

```sh
mpremote connect <PORT> fs cp main.py chirp_prov.py chirp_send.py chirp_sign.py :
```

## Set up the connection

1. Open https://warbletiot.com/flash in Chrome or Edge and connect the board’s serial port.
2. Choose your Wi-Fi network and Warblet device, then send the settings.
3. Open the device page to see incoming messages. Close other serial tools while the browser uses the port.

For manual setup, use a serial terminal at 115200 baud, 8 data bits, no parity, and 1 stop bit. Replace these values and send each command on one line:

```text
CHIRP?
CHIRP+ {"ssid":"YOUR_WIFI","pass":"YOUR_WIFI_PASSWORD","hwid":"YOUR_DEVICE_ID","token":"YOUR_DEVICE_TOKEN"}
```

The board saves `chirp_cfg.json` and restarts. Keep settings and keys private. A later push can contain only changed fields. An optional `key` holds a base64-encoded 32-byte signing key. Use either `claim` or `token`, not both.

An `aps` list can hold up to eight networks, tried in order. Sending `ssid` without `aps` replaces the list with one network. A stored `host` overrides the default `https.warbletiot.com`.

## What goes over the wire

The connection is HTTPS on port 443. `chirp_send.py` accepts the server only when its certificate chains to one of the four Let’s Encrypt roots listed in that file (ISRG Root X2, X1, YE, and YR) and names the host. Otherwise the send fails with `TLS to <host> refused`. Certificate dates are checked against the clock, so the app retries NTP before a send and does not send without a set clock.

The app sends `POST /ingest/{hwid}` with `Content-Type: application/octet-stream` and the token in `X-Chirp-Token`. A credential beginning with `CHIRP-` also goes in `X-Chirp-Claim`.

With a signing key, `X-Chirp-Signature` carries a hex HMAC-SHA256. When the clock is set, `X-Chirp-Nonce` carries Unix milliseconds and the signed bytes are `"<nonce>\n" + payload`. Without a clock, only the payload is signed, with no replay protection. This app has no downlink polling; keys arrive over USB.

The payload is two bytes: Celsius multiplied by 10, rounded, and stored as a signed 16-bit integer, high byte first. `00 d7` means 21.5 °C; `ff ce` means −5.0 °C. Die temperature measures the chip, not the room.

## Decoder

Paste [decoder.star](decoder.star) into the device spec’s decoder editor. It returns `temp_c` and handles negative readings.

## Make it real

Replace `read_temperature_c()` in `main.py` with your sensor’s Celsius reading, or return `None` when no reading is available.

Put sensor setup near the top of the app. For a different unit or payload shape, change `encode_temperature()` and `decoder.star` together. The encoder clamps values to −3276.8 through 3276.7.

## Security notes

`chirp_cfg.json` holds the Wi-Fi passwords, the device token, and the signing key as plain text in the board’s flash file system. Anyone who can reach the USB port can read them, replace them with a `CHIRP+` push, or reflash the board: physical access is full access. For a product, turn on the chip’s flash encryption and secure boot, lock down the USB console, and give every device its own token and key.

Do not commit a copy of `chirp_cfg.json` taken off a board; the repository’s `.gitignore` excludes that name.

## Troubleshooting

| Problem | Check |
|---|---|
| Waiting on USB | Send Wi-Fi settings and the device credential. |
| No stored network joined | Check the network name, password, and signal. Each attempt can take 12 seconds. |
| HTTP 401 | Check the device ID, token, and signing key. |
| Send failed | Check Wi-Fi, DNS, and the stored host. |
| `TLS … refused` or `no clock` | Allow NTP on the network. A stored `host` needs a certificate from one of the bundled roots. |

If the board will not boot, check the image and flash offset: ESP32 uses `0x1000`; S3, C3, and C6 use `0x0`. If the console says it has no die temperature, check the runtime or connect an external sensor.
