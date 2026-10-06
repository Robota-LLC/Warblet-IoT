# Olimex ESP32-C3-DevKit-Lipo — button reports and LED commands over HTTPS

## What happens

Press BUT1 to send a button report to Warblet, which stores it and shows it on your device page. The report contains the press count and two flags, and it is tagged `press`. The LED flashes when the server accepts it. The board also sends a report after 30 seconds without a detected press.

The board checks for LED commands between reports, waiting five seconds after each check. Browser setup adds the enabled [pair routine](pair-lights.star): a press turns on the lights of other Olimex or XIAO ESP32-C6 pair-demo boards in your account. Pressing a board’s own button turns its own light off, even when offline. See [Two boards, two places](../routines/README.md#two-boards-two-places) for setup and customization.

## What you need

- An Olimex ESP32-C3-DevKit-Lipo revision C.
- A USB-C data cable.
- A 2.4 GHz Wi-Fi network and a Warblet account.

## Quick start

Open [Set up a device](https://warbletiot.com/flash) in desktop Chrome or Edge, connect the board and select this demo. Follow the prompts to install it and send your network settings. Open the device page to see the result.

To customize the firmware first, use the build and flash steps below.

## Pins and settings

The board has the button and LED fitted. No extra wiring is needed.

| Setting in main.py | Value | Meaning |
|---|---|---|
| `LED_PIN` | 8 | LED1; low turns it on |
| `BUTTON_PIN` | 9 | BUT1; pressed connects to ground |
| `INTERVAL_S` | 30 | Seconds before a heartbeat |
| `COMMAND_POLL_S` | 5 | Seconds between command checks |
| `BUTTON_POLL_INTERVAL_MS` | 20 | Gap between button checks |
| `DEBOUNCE_MS` | 40 | A press must still read down this much later |

The button uses an internal pull-up and 40 ms debounce. Hold it for a moment: a short press can be missed during network requests. Check the pinout for your board revision; holding BUT1 during reset enters the bootloader.

This demo does not read battery voltage or charge status. Use an external sensor if your project needs them.

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
| ESP32-C3 | [ESP32_GENERIC_C3-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_C3-20260406-v1.28.0.bin) | `0x0` |

These commands are for ESP32-C3. For another chip, change the chip name, filename, and offset together.

```sh
esptool --chip esp32c3 --port <PORT> erase_flash
esptool --chip esp32c3 --port <PORT> write_flash 0x0 ESP32_GENERIC_C3-20260406-v1.28.0.bin
```
Copy the files, then press RESET:

```sh
mpremote connect <PORT> fs cp main.py chirp_prov.py chirp_send.py chirp_sign.py :
```

Keep the default flashing baud rate. If the board needs bootloader mode, hold BUT1 while resetting, then release it.

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

## Data and commands

The app connects to `https.warbletiot.com:443`, verifies the hostname and certificate against the roots in `chirp_send.py`, and requires an NTP-set clock before sending.

The app sends `POST /ingest/{hwid}` with `Content-Type: application/octet-stream` and the token in `X-Chirp-Token`. A credential beginning with `CHIRP-` also goes in `X-Chirp-Claim`. A report sent because of a press adds `X-Chirp-Tag: press`.

With a signing key, `X-Chirp-Signature` is a hex HMAC-SHA256 of `"<nonce>\ntag=<tag>\n" + payload`. The nonce is Unix milliseconds, also sent in `X-Chirp-Nonce`. Both lines are required; leave the tag empty for an untagged reading.

Commands are collected with `GET /ingest/{hwid}/down` and the token header. Signed devices also send a fresh nonce and a signature over `"chirp-down\n<hwid>\n<nonce>"`. A `200` returns one command; `204` means none. Truncated replies and replies over 1024 bytes are ignored.

Send signing keys over USB in the `key` field. This demo ignores over-the-air key updates, even if Warblet marks the delivery as sent.

The board waits five seconds after each command check; network requests add to that delay. Commands queued while it is offline wait for reconnection, subject to expiry. Delivery status does not confirm that the board acted on a command.

The payload has three bytes: an unsigned 16-bit press count, high byte first, followed by flags. The transmitted count stops at 65535 and resets on reboot.

| Flag | Field | Meaning |
|---|---|---|
| `0x01` | `button_down` | The button is held when the report is built |
| `0x02` | `on_press` | A detected press caused this report |

For example, `00 07 03` means seven presses, with both flags set.

## Staying connected

The app reconnects after a network failure. Readings missed while offline are not stored for later delivery. Commands wait at Warblet until reconnection or expiry; their delivery status does not confirm that the board acted on them.

Unexpected errors print to the console and restart the board after 10 seconds. Ctrl-C stops the app for debugging; press RESET to run it again.

## LED commands

Set the device spec’s controls to:

```json
{"controls":[{"type":"toggle","label":"LED","onText":"led:on","offText":"led:off"},{"type":"button","label":"Blink","text":"led:blink"}]}
```

`on_downlink()` matches `led:on`, `led:off`, `led:toggle` and `led:blink` as bytes and prints a line such as `chirp: down led:toggle -- LED ON`. Other commands are ignored, and only their size is printed, since a command can carry anything. The flash after an accepted report flips the LED for a moment and puts it back, so it does not undo `led:on`.

## Decoder

Browser setup adds the demo spec. For manual setup, paste [decoder.star](decoder.star) into your device spec’s decoder editor. It returns `presses`, `button_down`, and `on_press`.

## Make it real

Replace `pressed()` in `main.py` with your sensor’s true-or-false reading, and update `open_button()` for its input wiring.

A reed switch or float switch can use the same event counter. For measurements such as distance, change `encode_button_report()` and the decoder together. To drive a relay or a buzzer instead of the LED, change `on_downlink()` and the control text together. A press while the board is busy on the network is seen only if it is still held when that work ends.

## Security notes

`chirp_cfg.json` stores Wi-Fi passwords, the device token and signing key unencrypted. USB access allows reading or replacing them. Keep this file out of Git, use a separate credential for each device, and protect the console and stored credentials before deploying a product.

## Troubleshooting

| Problem | Check |
|---|---|
| Waiting on USB | Send Wi-Fi settings and the device credential. |
| No stored network joined | Check the network name, password, and signal. Each attempt can take 12 seconds. |
| HTTP 401 | Check the device ID, token, and signing key. |
| `command check replied 401` | Check the signing key, and that NTP works so the nonce is fresh. |
| LED control does nothing | Match the control text to `on_downlink()`. A command is collected at the next check, about every 6 seconds. |
| Send failed | Check Wi-Fi, DNS, and the stored host. |
| `TLS … refused` or `no clock` | Allow NTP on the network. A stored `host` needs a certificate from one of the bundled roots. |
| `stopped by an unexpected error` | Read the preceding error. The board restarts after 10 seconds; Ctrl-C stops it for debugging. |

Unexpected presses can mean the input lacks a pull-up. If flashing stops with `No serial data received`, use the default baud rate. If the app does not start after reset, release BUT1 and reset again.
