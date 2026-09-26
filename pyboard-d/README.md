# Pyboard D SF2W — chip temperature over HTTP

## What happens

The board reads its chip temperature every 30 seconds and sends two bytes to Warblet over plain HTTP. The green LED flashes when the server accepts the message; the red LED flashes on an error.

## What you need

- A Pyboard D SF2W with MicroPython v1.14.
- A micro-USB data cable.
- A Wi-Fi network and a Warblet account.

## Pins and settings

No external sensor is needed. `LED_OK = 2` and `LED_ERR = 1` select the green and red `pyb.LED` channels. `INTERVAL_S = 30` sets the interval.

`open_sensor()` uses `pyb.ADCAll(12, 0x70000)` for the internal channels. Keep the channel mask: omitting it also configures external ADC pins and can interfere with other hardware.

## Build

The app runs as MicroPython source. Install the computer tools:

```sh
python -m pip install mpremote
```

## Flash

Run from this demo’s directory. Replace `<PORT>` with the board’s serial port, such as `COM5` or `/dev/ttyACM0`.

Use MicroPython v1.14 for PYBD-SF2W. If it is already installed, copy the app files below. A board without this runtime needs a PYBD-SF2 runtime installed through its DFU bootloader first.

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

An `aps` list can hold up to eight networks, tried in order. Sending `ssid` without `aps` replaces the list with one network. A stored `host` overrides the default `http.warbletiot.com`.

## What goes over the wire

The app sends `POST /ingest/{hwid}` with `Content-Type: application/octet-stream` and the token in `X-Chirp-Token`. A credential beginning with `CHIRP-` also goes in `X-Chirp-Claim`.

With a signing key, `X-Chirp-Signature` carries a hex HMAC-SHA256. When the clock is set, `X-Chirp-Nonce` carries Unix milliseconds and the signed bytes are `"<nonce>\n" + payload`. Without a clock, only the payload is signed, with no replay protection. This app has no downlink polling; keys arrive over USB.

The payload is two bytes: Celsius multiplied by 10, rounded, and stored as a signed 16-bit integer, high byte first. `00 d7` means 21.5 °C; `ff ce` means −5.0 °C. Die temperature measures the chip, not the room.

The destination is `http.warbletiot.com:80`. This sender does not use TLS: the token and reading are visible on the network. A signature does not encrypt them, and this app cannot satisfy `enforceTls`. `chirp_send.py` contains its HTTP socket client and time-sync code.

## Decoder

Paste [decoder.star](decoder.star) into the device spec’s decoder editor. It returns `temp_c` and handles negative readings.

## Make it real

Replace `open_sensor()` and `read_temperature_c()` in `main.py` with setup and reading functions for your sensor.

Keep Celsius as the return unit, or change `encode_temperature()` and `decoder.star` to match your unit. For another MicroPython board, also check its LED API, ADC API, and Wi-Fi interface. TLS support requires a compatible runtime and sender; changing the hostname alone is not enough.

## Security notes

`chirp_cfg.json` holds the Wi-Fi passwords, the device token, and the signing key as plain text in the board’s flash file system. Anyone who can reach the USB port can read them, replace them with a `CHIRP+` push, or reflash the board: physical access is full access. This demo sends over plain HTTP, so the token and readings are also visible on the network. For a product, turn on the chip’s flash encryption and secure boot, lock down the USB console, and give every device its own token and key.

Do not commit a copy of `chirp_cfg.json` taken off a board; the repository’s `.gitignore` excludes that name.

## Troubleshooting

| Problem | Check |
|---|---|
| Waiting on USB | Send Wi-Fi settings and the device credential. |
| No stored network joined | Check the network name, password, and signal. Each attempt can take 12 seconds. |
| HTTP 401 | Check the device ID, token, and signing key. |
| Send failed | Check Wi-Fi, DNS, and the stored host. |

HTTP 301 can mean the stored host is wrong; use `http.warbletiot.com`. If time sync fails, signed messages omit the nonce. A reset can disconnect native USB before the setup reply is visible: reconnect and send `CHIRP?` to check the saved device ID.
