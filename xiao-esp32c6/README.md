# XIAO ESP32-C6 — temperature and LED commands over MQTTS

## What happens

The board sends its chip temperature every 30 seconds through MQTT over TLS, also called MQTTS. Dashboard commands turn the onboard LED on, off, or blink it. The app checks for commands between readings.

## What you need

- A Seeed XIAO ESP32-C6.
- A USB-C data cable.
- A 2.4 GHz Wi-Fi network with internet time access, and a Warblet account.

## Pins and settings

The onboard LED is GPIO15, active low. `LED_PIN`, `LED_ACTIVE_LOW`, and `INTERVAL_S` are near the top of `main.py`. `COMMAND_POLL_INTERVAL_MS = 200` sets the gap between command checks; network work can add delay. No external sensor is needed.

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
| ESP32-C6 | [ESP32_GENERIC_C6-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_C6-20260406-v1.28.0.bin) | `0x0` |

These commands are for ESP32-C6. For another chip, change the chip name, filename, and offset together.

```sh
esptool --chip esp32c6 --port <PORT> erase_flash
esptool --chip esp32c6 --port <PORT> write_flash 0x0 ESP32_GENERIC_C6-20260406-v1.28.0.bin
```
Copy the files, then press RESET:

```sh
mpremote connect <PORT> fs cp main.py chirp_mqtt.py chirp_prov.py chirp_sign.py :
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

An `aps` list can hold up to eight networks, tried in order. Sending `ssid` without `aps` replaces the list with one network. A stored `host` overrides the default `mqtt.warbletiot.com`.

## What goes over the wire

The app connects to `mqtt.warbletiot.com:8883`. The device ID is both the MQTT client ID and username. The token or claim code is the password.

| Direction | Topic | Data |
|---|---|---|
| Reading | `chirp/{hwid}/up` | Two temperature bytes |
| Command | `chirp/{hwid}/down` | `led:on`, `led:off`, or `led:blink` |

The payload is two bytes: Celsius multiplied by 10, rounded, and stored as a signed 16-bit integer, high byte first. `00 d7` means 21.5 °C; `ff ce` means −5.0 °C. Die temperature measures the chip, not the room.

With a signing key, each published payload starts with 32 raw HMAC-SHA256 bytes, followed by the two temperature bytes. The signature covers the temperature bytes. These messages have no nonce and no replay protection. QoS 0 gives no delivery acknowledgement; check the device page to confirm receipt.

`chirp_mqtt.py` requires the server certificate to chain to one of four Let’s Encrypt roots and checks the hostname. The file lists each root’s SHA-256 fingerprint; ISRG Root X2, today’s anchor, is `69729B8E15A86EFC177A57AFB7171DFC64ADD28C2FCA8CF1507E34453CCB1470`. ISRG Root X1, Root YE, and Root YR keep the board verifying when Let’s Encrypt changes chains. It sets the clock through NTP before connecting. A config value of `"tls": false` selects unencrypted port 1883; that connection cannot satisfy `enforceTls`.

## LED commands

Set the device spec’s controls to:

```json
{"controls":[{"type":"toggle","label":"LED","onText":"led:on","offText":"led:off"},{"type":"button","label":"Blink","text":"led:blink"}]}
```

`on_downlink()` matches these commands as bytes. Other commands are logged and ignored. The LED does not blink on temperature reports. Signing keys arrive through USB.

## Decoder

Paste [decoder.star](decoder.star) into the device spec’s decoder editor. It returns `temp_c` and handles negative readings.

## Make it real

Replace `read_temperature_c()` in `main.py` with your sensor’s Celsius reading.

For another unit, change `encode_temperature()` and `decoder.star` together. To control an output, edit `on_downlink()` and the dashboard control text to match. On another board, check the LED pin and the availability of `umqtt.simple`, `ssl.SSLContext`, and `ntptime`.

## Security notes

`chirp_cfg.json` holds the Wi-Fi passwords, the device token, and the signing key as plain text in the board’s flash file system. Anyone who can reach the USB port can read them, replace them with a `CHIRP+` push, or reflash the board: physical access is full access. For a product, turn on the chip’s flash encryption and secure boot, lock down the USB console, and give every device its own token and key.

Do not commit a copy of `chirp_cfg.json` taken off a board; the repository’s `.gitignore` excludes that name.

## Troubleshooting

| Problem | Check |
|---|---|
| Waiting on USB | Send Wi-Fi settings and the device credential. |
| Wi-Fi does not join | Check network name, password, and signal. |
| Clock or certificate error | Allow NTP and check the board’s date. |
| MQTT connection fails | Use the MQTT host and check the token. |
| Publish attempted, but no reading | Check the device page’s rejected-message count and signing key. |
| LED control does nothing | Match the control text to `on_downlink()` and check GPIO15. |
| Device requires TLS | Set `tls` to `true` so the app uses port 8883. |
