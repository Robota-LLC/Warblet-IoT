# Open starter: loopback

## What happens

Every 10 seconds, the board sends Warblet a count and the last message it received, such as `3 hello`. Send text from the device page’s **Message** box to see it echoed in a later reading. This demo uses Open authentication: no token, signature or clock is required.

Choose MicroPython for browser setup, Arduino for a sketch, or C for an existing sockets-based project. Each version checks for one command before sending its next reading.

## What you need

- For MicroPython: an ESP32, ESP32-S3, ESP32-C3, ESP32-C5 or ESP32-C6 board and a USB data cable.
- For manual MicroPython setup: a Raspberry Pi Pico 2 W with MicroPython 1.29.0, or a Pyboard D SF2W with MicroPython 1.14.
- For Arduino or C: a board with Wi-Fi that you already program that way.
- A 2.4 GHz Wi-Fi network and a Warblet account.

The setup page installs MicroPython on the ESP32 family. Use the manual instructions below for a [Raspberry Pi Pico 2 W](#on-a-raspberry-pi-pico-2-w) or [Pyboard D](#on-a-pyboard-d).

## Set it up with MicroPython

### From the setup page

1. Open [Set up the Open starter](https://warbletiot.com/flash?demo=demo-open-starter) in Chrome or Edge, connect the board, and pick **Open starter: loopback**.
2. Enter your Wi-Fi name and password. Setup installs the runtime and files as needed, assigns an ID and adds the board to your account with the Open starter spec.
3. Open the device page, wait for a reading, then send text from **Message**.

A command shown as Sent is not a receipt from the board. The echoed reading confirms that the board received your message.

### On an ESP32, by hand

Run these commands from `open-starter/`. Replace `<PORT>` with your serial port, such as `COM5` or `/dev/ttyACM0`. The images and esptool commands in this section are for ESP32 chips; erasing flash removes files and settings.

| Chip | Image | Offset | SHA-256 |
|---|---|---|---|
| ESP32 | [ESP32_GENERIC-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC-20260406-v1.28.0.bin) | `0x1000` | `cd7820d02c35d34dd403b44263129c6a511b350aea8446c229890753fe240784` |
| ESP32-S3 | [ESP32_GENERIC_S3-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_S3-20260406-v1.28.0.bin) | `0x0` | `8c6039d40024cae3c6b04bb434f77901548f5487df9789af15548c54f9e11d7f` |
| ESP32-C3 | [ESP32_GENERIC_C3-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_C3-20260406-v1.28.0.bin) | `0x0` | `c749c22b6fe00cb8211a3b227ed6c0ce9babb518fd8f5a661f7496eb878fa895` |
| ESP32-C5 | [ESP32_GENERIC_C5-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_C5-20260406-v1.28.0.bin) | `0x2000` | `ab605cb138e2abc7f179469a08e37b4b8050975268dc481594abba4ed1b33567` |
| ESP32-C6 | [ESP32_GENERIC_C6-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_C6-20260406-v1.28.0.bin) | `0x0` | `d338042d2dd87a00dad2163071f44bf67da1401e1d1d0686f2bb2e336e78f0e6` |

These commands use esptool 5 or newer and an ESP32-C3. For another ESP32 chip, change the chip name, file name and offset together. Then copy the two files and restart:

```sh
python -m pip install --upgrade esptool mpremote
python -m esptool --chip esp32c3 --port <PORT> erase-flash
python -m esptool --chip esp32c3 --port <PORT> write-flash 0x0 ESP32_GENERIC_C3-20260406-v1.28.0.bin
mpremote connect <PORT> fs cp main.py open_setup.py :
mpremote connect <PORT> reset
```

### On a Raspberry Pi Pico 2 W

The Warblet setup page does not install or configure this board. Install MicroPython and the two demo files by hand:

1. Download [MicroPython 1.29.0 for Pico 2 W](https://micropython.org/download/RPI_PICO2_W/), the file `RPI_PICO2_W-20260824-v1.29.0.uf2`.
2. Unplug the board. Hold **BOOTSEL** while plugging it into USB, then release BOOTSEL when the **RP2350** drive appears. This also works when the board already has firmware.
3. Copy the UF2 file onto **RP2350**. The drive disappears as the board restarts into MicroPython. These are the manufacturer's [UF2 installation steps](https://www.raspberrypi.com/documentation/microcontrollers/micropython.html#drag-and-drop-micropython).
4. From this demo's folder, replace `<PORT>` with the new serial port and copy the files:

   ```sh
   python -m pip install mpremote
   mpremote connect <PORT> fs cp main.py open_setup.py :
   mpremote connect <PORT> reset
   ```

The Pico 2 W has no RESET button. To restart later, press **Ctrl-D** at the MicroPython `>>>` prompt, or unplug and reconnect USB. A reset can disconnect the serial port briefly; reopen your terminal after it returns.

### Give MicroPython your Wi-Fi settings

For an ESP32, you can use the setup page after installing the files. For manual setup, open a serial terminal at 115200 baud and send the line below, replacing the Wi-Fi details and `<your-board-id>` with a fresh, unique ID:

```text
CHIRP+ {"ssid":"YOUR_WIFI","pass":"YOUR_WIFI_PASSWORD","hwid":"<your-board-id>"}
```

If the terminal is blank, type `CHIRP?` and press Enter; your typing may not appear. An unconfigured board replies `CHIRP! v=1 hw= radios=wifi t=demo-open-starter`. If you see `>>>` instead, press **Ctrl-D** to start the program, then send `CHIRP?` and the setup line.

The board answers `CHIRP= ok`, saves `open_cfg.json` and restarts. On the Pico 2 W, reconnect the terminal when its USB serial port returns. An `aps` list can hold up to eight networks, tried in order; this firmware refuses `claim` and `token` fields with `CHIRP= err unsupported`.

You can also write `open_cfg.json` with the same three fields and restart the board.

Then claim the board: on your Warblet dashboard, use Find device with its id and tick **Open starter**, so the board arrives with its decoder and the Message box.

### On a Pyboard D

For a Pyboard D SF2W with MicroPython 1.14, install the files manually. The board exposes its flash as a drive named `PYBFLASH`:

1. Copy `main.py` and `open_setup.py` onto `PYBFLASH`.
2. Write `open_cfg.json` there too, with your Wi-Fi and your id: `{"ssid":"YOUR_WIFI","pass":"YOUR_WIFI_PASSWORD","hwid":"<your-board-id>"}`.
3. When the copy has finished, eject `PYBFLASH` (unmount it on macOS or Linux), then reset the board from a serial terminal at 115200 baud: press Ctrl-C, then type `import machine; machine.reset()`. It joins Wi-Fi and sends its first line a few seconds later.

You can also send settings with `CHIRP+` at 115200 baud. Eject `PYBFLASH` before resetting or editing files from the board’s prompt to avoid conflicting writes.

## Set it up with Arduino

1. Open `arduino/warblet_open/warblet_open.ino` in the Arduino IDE.
2. Set `WIFI_NAME`, `WIFI_PASSWORD` and `BOARD_ID`. Until `BOARD_ID` is yours, the sketch sends nothing and says so on the Serial Monitor.
3. Pick your board and upload. Open the Serial Monitor at 115200 baud to watch each line and Warblet's answer.
4. Claim the board with Find device and tick **Open starter**.

The sketch uses your board’s Wi-Fi library (`WiFi.begin()`, `WiFi.status()` and `WiFiClient`). Its include block covers ESP8266, ESP32 and the listed Arduino Wi-Fi headers; adapt it for another board. For ESP8266, use Arduino core 3.1.2; for a Seeed XIAO ESP32-S3, use ESP32 Arduino core 3.3.12.

## Set it up in C

`c/warblet_open.c` is one file on the sockets API: `getaddrinfo`, `socket`, `connect`, `send`, `recv` and `close`. Add it to your project, bring your network up the way your platform does, then call it from one task:

```c
int warblet_open_step(const char *id, char *line, size_t size);

static char line[1040];
for (;;) {
    int status = warblet_open_step("<your-board-id>", line, sizeof line);
    printf("%s -> %d\n", line, status);   /* 202: Warblet has it */
    sleep(10);                            /* or your RTOS's delay */
}
```

The C file requires a POSIX-style sockets API. ESP-IDF supplies it; an lwIP project uses `-DWARBLET_LWIP` and the settings below. Your application must bring up the network and reconnect after a drop. After the first successful send, use **Find device** and select **Open starter**.

### On ESP-IDF

For ESP-IDF 5.5, start from `examples/wifi/getting_started/station`:

1. Copy `warblet_open.c` into the example's `main` folder and add it to `main/CMakeLists.txt`. ESP-IDF has `<sys/socket.h>`, so the file builds as it is, without `-DWARBLET_LWIP`:

   ```cmake
   idf_component_register(SRCS "station_example_main.c" "warblet_open.c"
                          PRIV_REQUIRES esp_wifi nvs_flash
                          INCLUDE_DIRS ".")
   ```

2. In `event_handler()`, reconnect on Wi-Fi disconnect rather than stopping at the example’s retry limit. Remove Wi-Fi passwords from logging.
3. Put the loop above in a function of its own, `static void warblet_task(void *arg)`, with `vTaskDelay(pdMS_TO_TICKS(10000))` as the delay, and start it at the end of `app_main()`: `xTaskCreate(warblet_task, "warblet", 6144, NULL, 5, NULL);`
4. Select the target before configuring Wi-Fi. For ESP32-C5 on ESP-IDF 5.5, run `idf.py --preview set-target esp32c5`, then `idf.py menuconfig` → **Example Configuration**. Run `idf.py build` and `idf.py -p <PORT> flash monitor`. A successful send prints a line such as `1 hello -> 202`.

### On an NXP MCUXpresso SDK project

For an NXP RW612 project using the MCUXpresso SDK, lwIP 2.2, FreeRTOS and the Wi-Fi connection manager (`wlan.h`):

1. Add `warblet_open.c` to your `source` folder and define `WARBLET_LWIP` for it, so it takes its sockets from lwIP and closes them with `lwip_close()`.
2. In `lwipopts.h`, make sure `LWIP_SOCKET`, `LWIP_DNS`, `LWIP_SO_RCVTIMEO` and `LWIP_SO_SNDTIMEO` are 1, and leave `LWIP_COMPAT_SOCKETS` on (lwIP's default): the file calls `socket()`, `connect()` and `getaddrinfo()` by those names.
3. Start with 1,024 words of task stack and measure usage in your application. Wait for an IP address, then call the helper every 10 seconds:

   ```c
   static void warblet_task(void *arg)
   {
       static char line[1040];
       while (!is_sta_ipv4_connected())
           vTaskDelay(pdMS_TO_TICKS(500));
       for (;;) {
           int status = warblet_open_step("<your-board-id>", line, sizeof line);
           PRINTF("%s -> %d\r\n", line, status);
           vTaskDelay(pdMS_TO_TICKS(10000));
       }
   }
   ```

   Start it before `vTaskStartScheduler()`: `xTaskCreate(warblet_task, "warblet", 1024, NULL, tskIDLE_PRIORITY + 1, NULL);`
4. Have your application call `wlan_connect()` when a reconnect is needed. The Warblet helper retries requests; it does not manage Wi-Fi.

### Your own lines

The file also has `warblet_post()`, `warblet_get_command()` and `warblet_post_udp()`, so you can send your own lines. Its buffers are static, so call it from one task.

A board that can only send UDP can send its line with `warblet_post_udp()`, but nothing comes back over UDP, so it cannot say back what you type.

## Data and commands

| Field | Value |
|---|---|
| **Transport** | plain HTTP to `http.warbletiot.com`, port 80 |
| **Security cap** | Open (L0): no token, no signature |
| **Announce** | `CHIRP! v=1 hw=<id> radios=wifi t=demo-open-starter` |

Each cycle collects one command, then sends a reading. The optional UDP helper sends data without receiving commands:

```text
POST http://http.warbletiot.com/ingest/<id>/down  -> 200 and a command as the body, or 204 for none
POST http://http.warbletiot.com/ingest/<id>       body "<count> <heard>"  -> 202
UDP  udp.warbletiot.com:7701                      "CHIRP1 <id> - <count> <heard>"  (nothing comes back)
```

Use `POST`, with an empty body, to collect a command; unauthenticated `GET` is refused. The examples use HTTP/1.0 with `Host` and `Content-Length` headers and read the reply until the connection closes. In the UDP format, `-` occupies the credential field.

Choose a unique device ID using letters, digits or `- _ . :`, up to 128 characters. Replace `<your-board-id>` everywhere; angle brackets are not valid. Two boards with the same ID share one identity and compete for commands.

Use a fresh ID for this Open demo. An ID whose device was deleted cannot be claimed by name or lowered to Open; reuse of that ID requires setup with a token-capable demo.

## The decoder

Setting the board up from the setup page, or claiming it with **Open starter** ticked, adds [decoder.star](decoder.star) for you. It turns `3 hello` into `count` 3 and `heard` "hello", so the device page shows the count and the text. A line that does not start with a count is stored with an error saying why.

## Step up: a token, then HTTPS

Open is for learning and quick experiments. When the board starts to matter:

1. **Add a token.** On the device page, use **Security → Keys and recovery → Rotate token** and save the token shown once. Modify the source’s `http()` helper to include `X-Chirp-Token: <token>\r\n` in both requests. Then open the linked spec and set **Device authentication** to **Token** under **Authentication target**. The device moves to Token after a valid authenticated reading; other devices using that spec upgrade when their own valid readings arrive.

   Prefer a header over a token in the URL, which can be retained in request logs. Plain HTTP exposes either form to anyone on the network path.
2. **Add HTTPS.** Use a TLS-capable client to connect to `https.warbletiot.com:443`, verifying both the certificate and hostname. These examples use plain sockets; changing only the hostname or port does not enable TLS. The [generic ESP32 demo](../esp32-generic/README.md) provides an HTTPS example.

See the other demos and [Warblet docs](https://warbletiot.com/docs) for signed messages and other transports.

## Limits

- Anyone who knows the board's id can send lines as it and collect its commands. Over plain HTTP, anyone on the network path can read both.
- Give each board its own id. Two boards with one id are one device to Warblet: their counts interleave, and whichever asks first takes a command.
- Open allows about one line every 5 seconds per id.
- Lines sent before the board is claimed are seen, not stored. The count keeps going, so the first stored line can be `4 hello`.
- When the board restarts, the count starts again at 1 and what it heard goes back to `hello`.
- The board never sends a line twice. Between restarts, a gap in the counts is a line that never arrived.
- Keep commands short and use UTF-8 text. The Message box allows 256 characters. Larger API commands can be truncated: Arduino and C keep at most 1,024 bytes, stopping at a zero byte; MicroPython keeps roughly 1,900 bytes and ignores invalid UTF-8.
- Ctrl-C stops the MicroPython program; press RESET or press Ctrl-D at the `>>>` prompt to start it again. On the Pico 2 W, use Ctrl-D or reconnect USB.

## Make it real

Replace the text the board sends with a reading of your own: change how `line` is built (in `main.py`, in `loop()` in the sketch, or in `warblet_open_step()` in C) and change `decoder.star` to match.

To drive a relay, a display or a buzzer, act where a command arrives: under `if status == 200:` in `main.py`, under `if (http("POST", path, "") == 200)` in the sketch, or where `warblet_get_command()` returns 1 in C. Compare what arrived (`heard`, or `command` in C) with the words you choose there. Acting on `heard` on every pass instead repeats the action every 10 seconds. When the reading matters, step up to a token.

## Security notes

`open_cfg.json` stores Wi-Fi passwords unencrypted; the Arduino sketch embeds them in firmware. Physical access can expose or replace them. Keep configs and filled-in constants out of Git. Open authentication also lets anyone who knows the device ID submit readings and collect commands.

Deleting `open_cfg.json` is not a secure erase: its contents can remain in flash until overwritten. Erase the flash before passing the board on.

## Troubleshooting

Each line ends with Warblet's answer:

| You see | It means |
|---|---|
| `-> 202 {"status":"unclaimed"}` | Warblet heard the board. Claim it to start storing its lines. |
| `-> 202 {"status":"accepted"}` | Stored. It is on the device page. |
| `-> 202` (Arduino, C) | Warblet has it: stored once the board is claimed, seen before that. |
| `-> 400` | The id has a character it cannot have. |
| `-> 401` | This identity requires a credential. Use its token in suitable firmware (see Step up), or choose a fresh unique ID for the Open starter. Deleting the device does not make its ID available at Open again. |
| `-> 429` | Slow down or check account storage limits. Newly seen IDs can also be rate-limited; wait and retry before using Find device. |
| `-> 502` | Temporary service error; the next reading retries. |
| `-> -1` or `open: will try again` | Check Wi-Fi, DNS and internet access. The board retries automatically. |
| `open: not set up yet` | Send Wi-Fi settings and a unique ID. |
