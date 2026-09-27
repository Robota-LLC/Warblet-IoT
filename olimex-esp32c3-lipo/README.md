# Olimex ESP32-C3-DevKit-Lipo — button reports over HTTPS

## What happens

Press BUT1 to send a button report to Warblet. The report contains the press count and two flags. The LED flashes when the server accepts it. The board also sends a report after 30 seconds without a detected press.

## What you need

- An Olimex ESP32-C3-DevKit-Lipo revision C.
- A USB-C data cable.
- A 2.4 GHz Wi-Fi network and a Warblet account.

## Pins and settings

The board has the button and LED fitted. No extra wiring is needed.

| Setting in main.py | Value | Meaning |
|---|---|---|
| `LED_PIN` | 8 | LED1; low turns it on |
| `BUTTON_PIN` | 9 | BUT1; pressed connects to ground |
| `INTERVAL_S` | 30 | Seconds before a heartbeat |
| `BUTTON_POLL_INTERVAL_MS` | 200 | Gap between button checks |
| `DEBOUNCE_MS` | 40 | Wait before confirming a press |

The button uses the internal pull-up resistor. Check the pinout for your board revision. Holding BUT1 during reset enters the bootloader.

This demo does not measure battery voltage or charge status. On revision C, the voltage divider needs both `BAT_PWR_E1` and `BAT_SENS_E1` closed and reaches GPIO5, which this MicroPython target does not expose as an ADC input. The charge-status signal drives an LED, not a GPIO input.

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

## What goes over the wire

The connection is HTTPS on port 443. `chirp_send.py` accepts the server only when its certificate chains to one of the four Let’s Encrypt roots listed in that file (ISRG Root X2, X1, YE, and YR) and names the host. Otherwise the send fails with `TLS to <host> refused`. Certificate dates are checked against the clock, so the app retries NTP before a send and does not send without a set clock.

The app sends `POST /ingest/{hwid}` with `Content-Type: application/octet-stream` and the token in `X-Chirp-Token`. A credential beginning with `CHIRP-` also goes in `X-Chirp-Claim`.

With a signing key, `X-Chirp-Signature` carries a hex HMAC-SHA256. When the clock is set, `X-Chirp-Nonce` carries Unix milliseconds and the signed bytes are `"<nonce>\n" + payload`. Without a clock, only the payload is signed, with no replay protection. This app has no downlink polling; keys arrive over USB.

The payload has three bytes: an unsigned 16-bit press count, high byte first, followed by flags. The transmitted count stops at 65535 and resets on reboot.

| Flag | Field | Meaning |
|---|---|---|
| `0x01` | `button_down` | The button is held when the report is built |
| `0x02` | `on_press` | A detected press caused this report |

For example, `00 07 03` means seven presses, with both flags set.

## Decoder

Paste [decoder.star](decoder.star) into the device spec’s decoder editor. It returns `presses`, `button_down`, and `on_press`.

## Make it real

Replace `pressed()` in `main.py` with your sensor’s true-or-false reading, and update `open_button()` for its input wiring.

A reed switch or float switch can use the same event counter. For measurements such as distance, change `encode_button_report()` and the decoder together. Button checks pause during Wi-Fi connection and HTTP sends, so this loop can miss short pulses.

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

Unexpected presses can mean the input lacks a pull-up. If flashing stops with `No serial data received`, use the default baud rate. If the app does not start after reset, release BUT1 and reset again.
