# Warblet firmware demos

Connect a board to [Warblet](https://warbletiot.com), see its readings or photos, and send commands back. These examples cover MicroPython, Arduino, ESP-IDF C and STM32 C. Each includes source, a payload decoder and a guide to adapting it.

## Quick start

1. Choose a demo below and check its board and network requirements.
2. Open [Set up a device](https://warbletiot.com/flash) in desktop Chrome or Edge. Connect the board with a USB data cable and follow the setup steps.
3. Open the device page to see its data and try its controls. Browser setup adds the demo spec and decoder for you.

To build or install files yourself, follow the demo's build, flash and connection instructions. Close other serial tools before connecting from the browser. The STM32 USB-relay demo needs a computer running the relay; the Thread demo needs a border router with DNS64/NAT64.

## Choose a demo

The [Open starter](open-starter/README.md) sends text and echoes a message with no extra wiring. For sensors and controls, pick the example matching your board.

| Directory | Pick this one if | Board | Toolchain | Transport |
|---|---|---|---|---|
| [`olimex-esp32c3-lipo/`](olimex-esp32c3-lipo/README.md) | You want a button to send a report and the LED to take commands. | Olimex ESP32-C3-DevKit-Lipo rev C | MicroPython 1.28.0 | HTTPS |
| [`open-starter/`](open-starter/README.md) | You want the simplest loop: a line of text out, what you type back, in MicroPython, Arduino or C. | ESP32 / S3 / C3 / C5 / C6, or your own Wi-Fi board | MicroPython 1.28.0, Arduino, C | HTTP |
| [`esp32-generic/`](esp32-generic/README.md) | You want chip temperature and LED commands on an ESP32 board. | ESP32 / S3 / C3 / C5 / C6 | MicroPython 1.28.0 | HTTPS |
| [`xiao-esp32c6/`](xiao-esp32c6/README.md) | You want temperature, a button, and LED commands pushed to the board. | Seeed XIAO ESP32-C6 | MicroPython 1.28.0 | MQTTS |
| [`pyboard-d/`](pyboard-d/README.md) | You have a Pyboard D SF2W with MicroPython 1.14. | Pyboard D SF2W | MicroPython 1.14 | HTTP |
| [`freenove-wrover-cam/`](freenove-wrover-cam/README.md) | You want scheduled pictures and polled camera commands. | Freenove ESP32-WROVER CAM | ESP-IDF 5.5 | HTTPS |
| [`freenove-wrover-cam-mqtts/`](freenove-wrover-cam-mqtts/README.md) | You want the camera to receive snap commands over MQTT. | Freenove ESP32-WROVER CAM | ESP-IDF 5.5 | MQTTS |
| [`xiao-esp32s3-sense-cam/`](xiao-esp32s3-sense-cam/README.md) | You want a doorbell button that sends a photo. | Seeed Studio XIAO ESP32-S3 Sense | ESP-IDF 5.5 | MQTTS |
| [`firebeetle-c5/`](firebeetle-c5/README.md) | You want temperature and LED commands on a TCP connection. | DFRobot FireBeetle 2 ESP32-C5 | ESP-IDF 5.5 | TCP |
| [`nucleo-wba65-baremetal/`](nucleo-wba65-baremetal/README.md) | You want to read C firmware that sends through a computer relay. | ST NUCLEO-WBA65RI | GNU Arm + Make | HTTP via USB relay |
| [`nucleo-wba65-thread/`](nucleo-wba65-thread/README.md) | You want to send readings through a Thread border router. | ST NUCLEO-WBA65RI | STM32CubeWBA + GNU Arm + Make | Thread / UDP |

The two WBA65 demos send simulated temperatures until you add a sensor. Other temperature demos measure the chip, not room temperature. The Pyboard D temperature demo and both WBA65 demos do not receive commands.

## Connect boards with routines

Browser setup includes an enabled routine with three demos: the Olimex and XIAO ESP32-C6 buttons turn on other paired boards' lights; the XIAO ESP32-S3 Sense doorbell emails its button-triggered photo. Set an alert email address for the doorbell. Manage these in **Routines**.

See [routine examples](routines/README.md) to pair boards, change the actions, or have a separate button request a camera photo.

## Build and port

- Run commands from the selected demo directory. Replace `<PORT>` with the current serial port, such as `COM5` or `/dev/ttyACM0`.
- Use the runtime and target specified by the demo. On ESP32, match the chip, firmware image and flash offset. On STM32, check the linker layout and reserved settings page.
- Start with **Make it real** in the demo guide: it names the sensor and command functions to change. Check pin assignments, voltage levels and active-low outputs for your board.
- Change the payload encoder and `decoder.star` together. Keep command text in the device spec consistent with the firmware's command handler.
- Check the result on the device page. A successful socket write or MQTT publish alone does not confirm that a reading was accepted or a command acted on.

| File or module | Purpose |
|---|---|
| `main.py`, `main/main.c`, `src/` or `chirp/src/` | Application, sensor and controls |
| `chirp_prov` or `open_setup` | USB setup |
| `chirp_send` or `chirp_mqtt` | Network transport |
| `chirp_sign` | Message signatures |
| `decoder.star` | Payload bytes to named fields |
| `demo.json` | Supported boards, runtime images, checksums and setup defaults |

Agents should use the selected demo's `demo.json` and README together, preserve its setup protocol, and keep credentials out of source and logs. Build commands create firmware; flash commands write to hardware and may erase stored settings.

## Authentication and encryption

Open uses no credential: anyone who knows the device ID can submit data and collect its commands. A token authenticates requests; a signing key adds an HMAC-SHA256 signature. Neither encrypts the data. HTTPS and MQTTS encrypt the connection and verify the server; the HTTP, raw TCP and UDP examples cannot meet a device's TLS requirement.

The guides describe each demo's limits and credential storage. Keep Wi-Fi passwords, tokens, signing keys and Thread datasets private. Never commit a board's `chirp_cfg.json`, `open_cfg.json` or filled-in credentials.

See [Warblet docs](https://warbletiot.com/docs) for platform setup and protocol reference.

<details>
<summary>Porting message signatures</summary>

HMAC-SHA256 signs this byte sequence. Include both lines, leaving an absent nonce or tag empty:

```text
signed input = <nonce> "\n" "tag=" <tag> "\n" <body>
```

- HTTP sends the hex signature in `X-Chirp-Signature`.
- MQTT and raw TCP prefix the payload with 32 raw signature bytes.
- UDP uses `{token}:{hex hmac of the signed input}` in the credential field.

Only HTTP carries a nonce in these demos; the other transports have no replay protection.

</details>

## License

Robota's code is [MIT licensed](LICENSE). Vendored Arm and ST files retain their own licenses; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
