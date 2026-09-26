# FireBeetle 2 ESP32-C5 — temperature and LED commands over TCP

## What happens

The board joins Wi-Fi and sends its chip temperature to Warblet every 30 seconds on an open TCP connection. Dashboard commands on the same connection control the onboard LED.

## What you need

- A DFRobot FireBeetle 2 ESP32-C5 with 4 MB flash and no in-package PSRAM.
- A USB-C data cable.
- A Wi-Fi network and a Warblet account.
- ESP-IDF v5.5 on your computer.

## Pins and settings

The user LED is GPIO15, labelled D13, and is set in `main/chirp_led.c`. The LED can look faint. GPIO15 also serves as a PSRAM signal on some modules; check the module before copying this pin setting to another board.

The temperature comes from the chip, so no sensor wiring is needed. It measures the chip rather than the room. `REPORT_PERIOD_MS` in `main/chirp_send.c` sets the interval.

## Build

Open an ESP-IDF v5.5 terminal, then change to this demo’s directory.

```sh
idf.py --preview set-target esp32c5
idf.py build
idf.py merge-bin
```

ESP32-C5 requires the `--preview` flag with `set-target` in this toolchain.

`test/provtest.c` checks the provisioning parser shared with the two camera demos on a computer, without a board. The compile command is at the top of the file.

## Flash

Replace `<PORT>` with the board’s serial port, such as `COM5` or `/dev/ttyUSB0`. For an initial install:

```sh
idf.py -p <PORT> flash monitor
```

For a board with the matching bootloader and partition table already installed, write only the app to preserve its stored settings:

```sh
esptool --chip esp32c5 --port <PORT> write_flash 0x10000 build/firebeetle-c5-demo.bin
```

You can also select the image in https://warbletiot.com/flash. Use `build/merged-binary.bin` at `0x0` for an initial install, or `build/firebeetle-c5-demo.bin` at `0x10000` for an app update. A merged-image write can erase stored credentials.

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

An `aps` list can hold up to eight networks. The demo tries visible networks from strongest to weakest signal, then any remaining networks in saved order. Sending `ssid` without `aps` replaces the list with one network. A stored `host` overrides the default `tcp.warbletiot.com`.

## What goes over the wire

The destination is `tcp.warbletiot.com:7700`. Lines end with a newline. At connection time the board sends:

```text
CHIRP1 YOUR_DEVICE_ID YOUR_DEVICE_TOKEN
```

A claim code uses `claim:YOUR_CLAIM_CODE` in the credential field. With no credential, that field is `-`.

Each reading follows on a `b64:` line. The payload is two bytes: Celsius multiplied by 10, rounded, and stored as a signed 16-bit integer, high byte first. For example, `01 1f` is 28.7 °C:

```text
b64:AR8=
```

With a signing key, the board prefixes the payload with 32 raw HMAC-SHA256 bytes, then base64-encodes the combined bytes. The signature covers the two payload bytes. There is no nonce or replay protection.

This connection is unencrypted. Tokens and readings are visible on the network, and the demo cannot satisfy `enforceTls`.

Commands arrive as `down:` followed by base64. For example, `down:bGVkOm9u` means `led:on`. The supported commands are `led:on`, `led:off`, and `led:blink`; blink selects repeated blinking. Use those exact strings in the device spec’s dashboard controls. Keys arrive through USB.

## Decoder

Paste [decoder.star](decoder.star) into the device spec’s decoder editor. It returns `temp_c` and handles negative readings.

## Make it real

Replace `temperature_sensor_start()` and `read_temperature_c()` in `main/main.c` with your sensor’s setup and reading functions.

The reading function returns an error if no reading is available. `report_temperature()` then skips that report. For another payload, change `encode_temperature()` and `decoder.star` together. The transport accepts up to `CHIRP_PAYLOAD_MAX` bytes, currently 64. Keep encoded values within the signed 16-bit range.

## Security notes

The device credential, the signing key, and the Wi-Fi passwords are stored unencrypted in the NVS partition. Anyone who can reach the USB port can read them with `esptool read_flash`, replace them with a `CHIRP+` push, or reflash the board: physical access is full access. For a product, turn on flash encryption with NVS encryption and secure boot, lock down the provisioning console, and give every device its own token and key. This demo’s raw TCP link is also unencrypted on the network.

## Troubleshooting

| Problem | Check |
|---|---|
| No networks stored | Send Wi-Fi settings and a device credential over USB. |
| Cannot resolve or connect | Check Wi-Fi, DNS, and that the host is `tcp.warbletiot.com`. |
| `err unauthorized` | Check the credential; the server closes this connection. |
| `err throttled` | Increase the reporting interval. |
| Sends appear to work, but no readings | Check the device page and rejected-message count. TCP writes do not confirm acceptance. |
| LED does not respond | Check the dashboard command text and the GPIO15 module wiring. |

The board retries broken connections. Serial setup uses the native USB-Serial-JTAG port at 115200 baud.
