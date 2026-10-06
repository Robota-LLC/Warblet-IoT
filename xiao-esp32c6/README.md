# XIAO ESP32-C6 — temperature, a button and LED commands over MQTTS

## What happens

The board sends its chip temperature to Warblet every 30 seconds through MQTT over TLS, also called MQTTS, and your Warblet device page shows it. Dashboard commands turn the onboard LED on, off, toggle it, or blink it. The broker pushes each command to the board as it is sent, and the app reads it between readings.

The BOOT button sends a reading tagged `press`. Browser setup adds the enabled [pair routine](pair-lights.star): a press turns on the lights of other Olimex or XIAO ESP32-C6 pair-demo boards in your account. Pressing a board’s own button turns its own light off, even when offline. See [Two boards, two places](../routines/README.md#two-boards-two-places) for setup and customization.

## What you need

- A Seeed XIAO ESP32-C6.
- A USB-C data cable.
- A 2.4 GHz Wi-Fi network with internet time access, and a Warblet account.

## Quick start

Open [Set up a device](https://warbletiot.com/flash) in desktop Chrome or Edge, connect the board and select this demo. Follow the prompts to install it and send your network settings. Open the device page to see the result.

To customize the firmware first, use the build and flash steps below.

## Pins and settings

The onboard LED is GPIO15, active low; BOOT is GPIO9 with an internal pull-up. Set pins and `INTERVAL_S` in `main.py`. The button is checked every 20 ms with 40 ms debounce; commands are checked every 200 ms, with additional delay during network work. Holding BOOT during reset enters the bootloader.

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

## Data and commands

The app connects to `mqtt.warbletiot.com:8883`. The device ID is both the MQTT client ID and username. The token or claim code is the password.

| Direction | Topic | Data |
|---|---|---|
| Reading | `chirp/{hwid}/up` | Two temperature bytes |
| Reading after a press | `chirp/{hwid}/up/press` | Two temperature bytes |
| Command | `chirp/{hwid}/down` | `led:on`, `led:off`, `led:toggle`, or `led:blink` |

The extra topic level is the message’s tag, `chirp/{hwid}/up/<tag>`. It says what the message is without changing the payload, so the decoder is the same for both.

The payload is two bytes: Celsius multiplied by 10, rounded, and stored as a signed 16-bit integer, high byte first. `00 d7` means 21.5 °C; `ff ce` means −5.0 °C. Die temperature measures the chip, not the room.

With a signing key, each published payload starts with 32 raw HMAC-SHA256 bytes, followed by the two temperature bytes. The signature covers an empty nonce line, the tag line and then the temperature bytes: `"\ntag=\n"` before a plain reading and `"\ntag=press\n"` before a press. Those lines are signed but not sent, because the topic already carries the tag. These messages have no nonce and no replay protection. QoS 0 gives no delivery acknowledgement; check the device page to confirm receipt.

`chirp_mqtt.py` verifies the hostname and server certificate using its bundled roots and sets the clock through NTP. Keep `tls` enabled for encrypted port 8883. Setting `"tls": false` uses unencrypted port 1883 and cannot meet a device’s TLS requirement.

## LED commands

Set the device spec’s controls to:

```json
{"controls":[{"type":"toggle","label":"LED","onText":"led:on","offText":"led:off"},{"type":"button","label":"Blink","text":"led:blink"}]}
```

`on_downlink()` matches `led:on`, `led:off`, `led:toggle` and `led:blink` as bytes and prints a line such as `chirp: down led:toggle -- LED ON`. Other commands are ignored, and only their size is printed, since a command can carry anything. The LED does not blink on temperature reports. Signing keys arrive through USB.

## Staying connected

The app reconnects after a network failure. Readings missed while offline are not stored for later delivery. Commands wait at Warblet until reconnection or expiry; their delivery status does not confirm that the board acted on them.

Unexpected errors print to the console and restart the board after 10 seconds. Ctrl-C stops the app for debugging; press RESET to run it again.

## Decoder

Browser setup adds the demo spec. For manual setup, paste [decoder.star](decoder.star) into your device spec’s decoder editor. It returns `temp_c` and handles negative readings.

## Make it real

Replace `read_temperature_c()` in `main.py` with your sensor’s Celsius reading.

For another unit, change `encode_temperature()` and `decoder.star` together. To control an output, edit `on_downlink()` and the dashboard control text to match. On another board, check the LED pin and the availability of `umqtt.simple`, `ssl.SSLContext`, and `ntptime`.

## Security notes

`chirp_cfg.json` stores Wi-Fi passwords, the device token and signing key unencrypted. USB access allows reading or replacing them. Keep this file out of Git, use a separate credential for each device, and protect the console and stored credentials before deploying a product.

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
| `mqtt link lost: no answer from the broker` | The session went quiet for 75 seconds and was dropped; the app connects again at the next reading. |
| `stopped by an unexpected error` | Read the preceding error. The board restarts after 10 seconds; Ctrl-C stops it for debugging. |
