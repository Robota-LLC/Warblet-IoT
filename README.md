# Warblet firmware demos

Eleven firmware examples for [Warblet](https://warbletiot.com), an IoT platform for creators and startups. Warblet sets a board up over USB, stores and shows what it sends, sends it commands, and runs routines that connect boards. Each demo includes application code, a payload decoder, and instructions for connecting a board to your device page.

## Choose a demo

Start with the [Olimex button demo](olimex-esp32c3-lipo/README.md) if you have that board. For another ESP32, try the [generic temperature demo](esp32-generic/README.md) and check its LED pin before running it. To see the whole loop with nothing to wire, in MicroPython, Arduino or C, start with the [Open starter](open-starter/README.md).

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

The two WBA65 demos send simulated values until you add a sensor. The bare-metal demo needs a computer relay; the Thread demo needs a border router with DNS64/NAT64.

Three demos come with a routine that Warblet adds to your account, switched on, the first time you set them up from warbletiot.com/flash: the [doorbell camera](xiao-esp32s3-sense-cam/README.md) emails the photo its button takes, and the [Olimex button](olimex-esp32c3-lipo/README.md) and [XIAO ESP32-C6](xiao-esp32c6/README.md) demos turn on each other's light, with no hub between them. The [routine examples](routines/README.md) add one you set up yourself: a button press that asks a camera for a picture, which is emailed.

Every MicroPython demo except the Pyboard D takes commands. The Open starter asks for one every 10 seconds and says it back in its next line. The MQTTS demo is pushed each command as it is sent. The HTTPS demos wait 5 seconds after each check for a command, and a check takes about a second on an ESP32-C3, so a command waits for the next check, about every 6 seconds; one sent while the board is offline waits at Warblet until the first check after it is back. The Pyboard D demo does not ask for commands.

## Get a board running

1. Open the README for your board. Check the parts, pins, and runtime or build tools.
2. Follow its Build and Flash steps. MicroPython demos copy source files; C demos build a firmware image.
3. Use https://warbletiot.com/flash to send network settings and the device credential over USB. The Open starter takes network settings and an id, and no credential.
4. Open the device page and check the incoming data. Install the demo’s `decoder.star` in the device spec when decoded fields are needed.

The camera decoders are optional: the raw JPEG can be displayed without one. Their decoders add the frame’s byte count.

## Adapt the code

Each README’s **Make it real** section points to the sensor function. Keep reading, encoding, and sending as separate steps. When the payload format changes, change the decoder too.

| File or module | Purpose |
|---|---|
| `main.py` or `main/main.c` | Application, sensor, and controls |
| `chirp_prov` or `open_setup` | USB setup commands |
| `chirp_send` or `chirp_mqtt` | Network transport |
| `chirp_sign` | Message signatures |
| `decoder.star` | Turns payload bytes into named fields |
| `demo.json` | Board, runtime, and firmware file details |

The STM32 applications use `src/` or `chirp/src/`; their READMEs identify the corresponding files. Board pins, runtime APIs, flash layout, and sensor drivers need checking when moving code to another board.

## Credentials and transport

The Open starter sends no credential at all: anyone who knows its id can post as it. Tokens identify a device. A signing key adds an HMAC-SHA256 signature; it does not encrypt the message. Every demo signs the same shape of bytes:

```text
signed input = <nonce> "\n" "tag=" <tag> "\n" <body>
```

Both lines are always there, and a field the message does not send is left empty: a message with no nonce and no tag signs `"\ntag=\n" + body`. Where the signature goes depends on the transport:

- HTTP: hex in `X-Chirp-Signature`. Only HTTP carries a nonce.
- MQTT and raw TCP: the 32 raw signature bytes in front of the payload.
- UDP: `{token}:{hex hmac of the signed input}` in the datagram's token field.

The HTTPS and MQTTS examples check the server’s certificate and host name before sending. The HTTP, raw TCP, and UDP examples cannot satisfy a device’s `enforceTls` setting.

Keep device configs, Wi-Fi passwords, Thread datasets, and signing keys out of your public copy; the `.gitignore` here excludes `chirp_cfg.json`. The demos store credentials unencrypted and accept new settings from anyone at the USB port, so physical access to a board is full access. Each README’s **Security notes** say what is stored where and what a product would add. Examples labelled `YOUR_...` must be replaced with your own values.

For platform setup and protocol details, see [Warblet docs](https://warbletiot.com/docs).

## License

Robota’s code in this repository is under the MIT license in `LICENSE`. A few vendored files from Arm and STMicroelectronics keep their own licenses, and the MIT license does not cover them. [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) lists them with their license texts.
