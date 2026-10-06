# Olimex ESP32-C3-DevKit-Lipo — button reports and LED commands over HTTPS

## What happens

Press BUT1 to send a button report to Warblet, which stores it and shows it on your device page. The report contains the press count and two flags, and it is tagged `press`. The LED flashes when the server accepts it. The board also sends a report after 30 seconds without a detected press.

Between reports the board checks with Warblet for a command, waiting 5 seconds after each check. A dashboard control or a routine can turn the LED on or off, toggle it, or blink it. The first time you set this demo up from warbletiot.com/flash, Warblet adds the [pair routine](pair-lights.star) to your account, switched on: a press on this board turns on the light of every other board in your account running this demo or the [XIAO ESP32-C6 demo](../xiao-esp32c6/README.md), and a press on one of those turns this one on. [Two boards, two places](../routines/README.md#two-boards-two-places) covers a board in another house. Pressing your own button also turns your own light off, right after the board tries to send the press. It does this even when the send fails or Wi-Fi is down, so your light can go off for a press the other board never got. When the board acts on a command it prints a line such as `chirp: down led:on -- LED ON`.

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
| `COMMAND_POLL_S` | 5 | Seconds between command checks |
| `BUTTON_POLL_INTERVAL_MS` | 20 | Gap between button checks |
| `DEBOUNCE_MS` | 40 | A press must still read down this much later |

The button uses the internal pull-up resistor. A press counts only if it still reads down `DEBOUNCE_MS` after it is seen, so contact bounce and glitches are not presses. While the board talks to Warblet (about a second for each report and each command check) it does not look at the button, so a press that starts and ends inside that second is missed: hold the button for a moment. The rest of the time the button is checked every 20 ms. Check the pinout for your board revision. Holding BUT1 during reset enters the bootloader.

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

The app sends `POST /ingest/{hwid}` with `Content-Type: application/octet-stream` and the token in `X-Chirp-Token`. A credential beginning with `CHIRP-` also goes in `X-Chirp-Claim`. A report sent because of a press adds `X-Chirp-Tag: press`.

With a signing key, `X-Chirp-Signature` carries a hex HMAC-SHA256 of `"<nonce>\ntag=<tag>\n" + payload`. Both lines are always signed; a field the message does not send is left empty. When the clock is set, `X-Chirp-Nonce` carries Unix milliseconds and the same number is the nonce line. A press signs `"<nonce>\ntag=press\n" + payload`; a heartbeat has no tag, so it signs `"<nonce>\ntag=\n" + payload`. Without a clock there is no nonce header, the nonce line is empty, and there is no replay protection.

To collect a command, the app sends `GET /ingest/{hwid}/down` with the token. A signed device also sends `X-Chirp-Nonce` and an `X-Chirp-Signature` over `"chirp-down\n<hwid>\n<nonce>"`, so the token alone cannot read its commands. The reply is `204` when nothing is waiting, or `200` with one command as the body. One command comes per check; another waiting command comes at the next check. A reply that ends early, or is over 1024 bytes, counts as a failed check and is not acted on.

Warblet can also send a signing key this way (a body starting `CHIRPKEY1 `). This demo takes its key over the USB setup, so it drops that key without printing it, and the board carries on as before. Warblet still marks the key delivered. To give the board that key, send it over the USB setup in the `key` field. Queuing it again over the air does not help here: this demo would drop it again.

Each check is one HTTPS request, about a second on an ESP32-C3, most of it the TLS handshake. With the 5-second wait, the board checks about every 6 seconds, so a command, or a press from a paired board, is picked up at the next check. Asking more often costs more requests and keeps the board busy longer; asking less often makes commands slower. Warblet marks a command delivered when it is written to the board's connection. Nothing reports whether the board then acted on it.

The payload has three bytes: an unsigned 16-bit press count, high byte first, followed by flags. The transmitted count stops at 65535 and resets on reboot.

| Flag | Field | Meaning |
|---|---|---|
| `0x01` | `button_down` | The button is held when the report is built |
| `0x02` | `on_press` | A detected press caused this report |

For example, `00 07 03` means seven presses, with both flags set.

## Staying connected

Every request has a 20-second limit, and a failed one goes back to the loop, which joins Wi-Fi again if it has gone. An error the loop does not expect is printed, and the board restarts itself 10 seconds later. Readings that came due while the board was offline are not sent later. A command sent while the board is offline waits at Warblet, for up to 24 hours, and is collected at the first check after the board is back.

A few stray bytes on the USB port, such as a program on the computer probing serial ports, do not stop the app: it reads the console a byte at a time. Such a program can reset the board. It normally starts again by itself; if it stays silent, press reset. A Ctrl-C byte (0x03) stops the app at the prompt, on purpose.

## LED commands

Set the device spec’s controls to:

```json
{"controls":[{"type":"toggle","label":"LED","onText":"led:on","offText":"led:off"},{"type":"button","label":"Blink","text":"led:blink"}]}
```

`on_downlink()` matches `led:on`, `led:off`, `led:toggle` and `led:blink` as bytes and prints a line such as `chirp: down led:toggle -- LED ON`. Other commands are ignored, and only their size is printed, since a command can carry anything. The flash after an accepted report flips the LED for a moment and puts it back, so it does not undo `led:on`.

## Decoder

Paste [decoder.star](decoder.star) into the device spec’s decoder editor. It returns `presses`, `button_down`, and `on_press`.

## Make it real

Replace `pressed()` in `main.py` with your sensor’s true-or-false reading, and update `open_button()` for its input wiring.

A reed switch or float switch can use the same event counter. For measurements such as distance, change `encode_button_report()` and the decoder together. To drive a relay or a buzzer instead of the LED, change `on_downlink()` and the control text together. A press while the board is busy on the network is seen only if it is still held when that work ends.

## Security notes

`chirp_cfg.json` holds the Wi-Fi passwords, the device token, and the signing key as plain text in the board’s flash file system. Anyone who can reach the USB port can read them, replace them with a `CHIRP+` push, or reflash the board: physical access is full access. For a product, turn on the chip’s flash encryption and secure boot, lock down the USB console, and give every device its own token and key.

Do not commit a copy of `chirp_cfg.json` taken off a board; the repository’s `.gitignore` excludes that name.

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
| `stopped by an unexpected error` | The app met an error it does not handle. It prints the error, waits 10 seconds and restarts the board. Ctrl-C on the USB console still stops it. The error text points at what to fix. |

Unexpected presses can mean the input lacks a pull-up. If flashing stops with `No serial data received`, use the default baud rate. If the app does not start after reset, release BUT1 and reset again.
