# Warblet firmware demos

Nine firmware examples for [Warblet](https://warbletiot.com), an IoT platform for creators and startups. Each demo includes application code, a payload decoder, and instructions for connecting a board to your device page.

## Choose a demo

Start with the [Olimex button demo](olimex-esp32c3-lipo/README.md) if you have that board. For another ESP32, try the [generic temperature demo](esp32-generic/README.md) and check its LED pin before running it.

| Directory | Pick this one if | Board | Toolchain | Transport |
|---|---|---|---|---|
| [`olimex-esp32c3-lipo/`](olimex-esp32c3-lipo/README.md) | You want a button to send a report. | Olimex ESP32-C3-DevKit-Lipo rev C | MicroPython 1.28.0 | HTTPS |
| [`esp32-generic/`](esp32-generic/README.md) | You want chip temperature from an ESP32 board. | ESP32 / S3 / C3 / C6 | MicroPython 1.28.0 | HTTPS |
| [`xiao-esp32c6/`](xiao-esp32c6/README.md) | You want temperature reports and LED commands. | Seeed XIAO ESP32-C6 | MicroPython 1.28.0 | MQTTS |
| [`pyboard-d/`](pyboard-d/README.md) | You have a Pyboard D SF2W with MicroPython 1.14. | Pyboard D SF2W | MicroPython 1.14 | HTTP |
| [`freenove-wrover-cam/`](freenove-wrover-cam/README.md) | You want scheduled pictures and polled camera commands. | Freenove ESP32-WROVER CAM | ESP-IDF 5.5 | HTTPS |
| [`freenove-wrover-cam-mqtts/`](freenove-wrover-cam-mqtts/README.md) | You want the camera to receive snap commands over MQTT. | Freenove ESP32-WROVER CAM | ESP-IDF 5.5 | MQTTS |
| [`firebeetle-c5/`](firebeetle-c5/README.md) | You want temperature and LED commands on a TCP connection. | DFRobot FireBeetle 2 ESP32-C5 | ESP-IDF 5.5 | TCP |
| [`nucleo-wba65-baremetal/`](nucleo-wba65-baremetal/README.md) | You want to read C firmware that sends through a computer relay. | ST NUCLEO-WBA65RI | GNU Arm + Make | HTTP via USB relay |
| [`nucleo-wba65-thread/`](nucleo-wba65-thread/README.md) | You want to send readings through a Thread border router. | ST NUCLEO-WBA65RI | STM32CubeWBA + GNU Arm + Make | Thread / UDP |

The two WBA65 demos send simulated values until you add a sensor. The bare-metal demo needs a computer relay; the Thread demo needs a border router with DNS64/NAT64.

The [routine examples](routines/README.md) connect the Olimex button demo to a camera and queue an email with the picture attached.

## Get a board running

1. Open the README for your board. Check the parts, pins, and runtime or build tools.
2. Follow its Build and Flash steps. MicroPython demos copy source files; C demos build a firmware image.
3. Use https://warbletiot.com/flash to send network settings and the device credential over USB.
4. Open the device page and check the incoming data. Install the demo’s `decoder.star` in the device spec when decoded fields are needed.

The camera decoders are optional: the raw JPEG can be displayed without one. Their decoders add the frame’s byte count.

## Adapt the code

Each README’s **Make it real** section points to the sensor function. Keep reading, encoding, and sending as separate steps. When the payload format changes, change the decoder too.

| File or module | Purpose |
|---|---|
| `main.py` or `main/main.c` | Application, sensor, and controls |
| `chirp_prov` | USB setup commands |
| `chirp_send` or `chirp_mqtt` | Network transport |
| `chirp_sign` | Message signatures |
| `decoder.star` | Turns payload bytes into named fields |
| `demo.json` | Board, runtime, and firmware file details |

The STM32 applications use `src/` or `chirp/src/`; their READMEs identify the corresponding files. Board pins, runtime APIs, flash layout, and sensor drivers need checking when moving code to another board.

## Credentials and transport

Tokens identify a device. A signing key adds an HMAC-SHA256 signature; it does not encrypt the message. The HTTPS and MQTTS examples check the server’s certificate and host name before sending. The HTTP, raw TCP, and UDP examples cannot satisfy a device’s `enforceTls` setting.

Keep device configs, Wi-Fi passwords, Thread datasets, and signing keys out of your public copy; the `.gitignore` here excludes `chirp_cfg.json`. The demos store credentials unencrypted and accept new settings from anyone at the USB port, so physical access to a board is full access. Each README’s **Security notes** say what is stored where and what a product would add. Examples labelled `YOUR_...` must be replaced with your own values.

For platform setup and protocol details, see [Warblet docs](https://warbletiot.com/docs).

## License

Robota’s code in this repository is under the MIT license in `LICENSE`. A few vendored files from Arm and STMicroelectronics keep their own licenses, and the MIT license does not cover them. [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) lists them with their license texts.
