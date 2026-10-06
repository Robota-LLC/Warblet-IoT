# Open starter: say hello, and say back what you type

## What happens

Every 10 seconds the board sends Warblet one line of plain text: a count, then the last thing it heard, like `3 hello`. Type something in the Message box on the board's device page, and the board says it back in its next line. The board sends no token and no signature and needs no clock: this is Warblet's Open level, the quickest way to see the whole loop of a connected board, from the line it sends to a command coming back.

The same demo comes three ways: MicroPython files that warbletiot.com/flash sets up for you, one Arduino sketch, and one C file on the sockets API. Each one first asks Warblet for a command, then sends its line, so what you type shows up in the very next line.

## What you need

- For MicroPython: an ESP32, ESP32-S3, ESP32-C3, ESP32-C5 or ESP32-C6 board and a USB data cable.
- For Arduino or C: a board with Wi-Fi that you already program that way.
- A 2.4 GHz Wi-Fi network and a Warblet account.

The MicroPython files also run on a Pyboard D SF2W with its MicroPython 1.14. The setup page does not set it up: copy the files onto it yourself (see "On a Pyboard D" below).

## The wire

| | |
|---|---|
| **Transport** | plain HTTP to `http.warbletiot.com`, port 80 |
| **Security cap** | Open (L0): no token, no signature |
| **Announce** | `CHIRP! v=1 hw=<id> radios=wifi t=demo-open-starter` |

The whole protocol is two requests every 10 seconds, plus one UDP form for a link with no TCP:

```text
POST http://http.warbletiot.com/ingest/<id>/down  -> 200 and a command as the body, or 204 for none
POST http://http.warbletiot.com/ingest/<id>       body "<count> <heard>"  -> 202
UDP  udp.warbletiot.com:7701                      "CHIRP1 <id> - <count> <heard>"  (nothing comes back)
```

Asking for a command is a `POST` with no body. A `GET` of that address is refused, so something that only repeats an address, like a proxy or a browser, never takes your command. The `-` in the UDP form is where a token would go. Each request is `HTTP/1.0` with a `Host` header and a `Content-Length`; Warblet answers and closes the connection, so the reply ends when the connection does. The board reads only the status and the body, never a header.

The id is yours: letters, digits and `- _ . :`, up to 128 characters. A few lowercase words and some random digits make a good one; make it up yourself rather than copying one, because everyone who uses the same id is the same device. The examples here write `<your-board-id>`, which Warblet refuses, so replace it. If Find device cannot find your id after the board has sent a line, someone else holds it: pick another.

## Set it up with MicroPython

### From the setup page

1. Open https://warbletiot.com/flash in Chrome or Edge, connect the board, and pick the Open starter.
2. Type your Wi-Fi name and password. The page writes MicroPython and the two files, sends your Wi-Fi and an id over USB (CHIRP-PROV v1.6, with no token), and adds the board to your account at Open with this demo's spec.
3. Open the device page: a line arrives every 10 seconds. Type in the Message box and send it.

The command list on the device page shows your message as Sent once Warblet has handed it to the plain-HTTP door. That is as far as the server can see. The board's next line is how you know it arrived.

### By hand

Write MicroPython 1.28.0 for your chip. Erasing flash removes stored files and settings.

| Chip | Image | Offset | SHA-256 |
|---|---|---|---|
| ESP32 | [ESP32_GENERIC-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC-20260406-v1.28.0.bin) | `0x1000` | `cd7820d02c35d34dd403b44263129c6a511b350aea8446c229890753fe240784` |
| ESP32-S3 | [ESP32_GENERIC_S3-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_S3-20260406-v1.28.0.bin) | `0x0` | `8c6039d40024cae3c6b04bb434f77901548f5487df9789af15548c54f9e11d7f` |
| ESP32-C3 | [ESP32_GENERIC_C3-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_C3-20260406-v1.28.0.bin) | `0x0` | `c749c22b6fe00cb8211a3b227ed6c0ce9babb518fd8f5a661f7496eb878fa895` |
| ESP32-C5 | [ESP32_GENERIC_C5-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_C5-20260406-v1.28.0.bin) | `0x2000` | `ab605cb138e2abc7f179469a08e37b4b8050975268dc481594abba4ed1b33567` |
| ESP32-C6 | [ESP32_GENERIC_C6-20260406-v1.28.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC_C6-20260406-v1.28.0.bin) | `0x0` | `d338042d2dd87a00dad2163071f44bf67da1401e1d1d0686f2bb2e336e78f0e6` |

These commands are for an ESP32-C3. For another chip, change the chip name, the file name and the offset together. Then copy the two files and press RESET:

```sh
python -m pip install esptool mpremote
esptool --chip esp32c3 --port <PORT> erase_flash
esptool --chip esp32c3 --port <PORT> write_flash 0x0 ESP32_GENERIC_C3-20260406-v1.28.0.bin
mpremote connect <PORT> fs cp main.py open_setup.py :
```

Then give the board your Wi-Fi and its id. Either connect it at warbletiot.com/flash, or send one line from a serial terminal at 115200 baud:

```text
CHIRP+ {"ssid":"YOUR_WIFI","pass":"YOUR_WIFI_PASSWORD","hwid":"<your-board-id>"}
```

The board answers `CHIRP= ok`, saves `open_cfg.json` and restarts. An `aps` list can hold up to eight networks, tried in order. A push that carries a `claim` or a `token` is answered `CHIRP= err unsupported`, because this firmware never sends one. You can also write `open_cfg.json` yourself with the same three fields.

Then claim the board: on your Warblet dashboard, use Find device with its id and tick **Open starter**, so the board arrives with its decoder and the Message box.

### On a Pyboard D

The two files have run on a Pyboard D SF2W with MicroPython 1.14. The setup page does not offer this board, so set it up by hand; the console's "connect this board at warbletiot.com/flash" is meant for the ESP32 boards. The board shows its flash to your computer as a drive named `PYBFLASH`:

1. Copy `main.py` and `open_setup.py` onto `PYBFLASH`.
2. Write `open_cfg.json` there too, with your Wi-Fi and your id: `{"ssid":"YOUR_WIFI","pass":"YOUR_WIFI_PASSWORD","hwid":"<your-board-id>"}`.
3. When the copy has finished, eject `PYBFLASH` (unmount it on macOS or Linux), then reset the board from a serial terminal at 115200 baud: press Ctrl-C, then type `import machine; machine.reset()`. It joins Wi-Fi and sends its first line a few seconds later.

A `CHIRP+` push from a serial terminal works on this board too: the board saves it and restarts, and keeps it. Copy files through the drive rather than writing them from the board's own prompt: while your computer has the drive open, files written from the prompt can be lost at the board's next soft restart.

## Set it up with Arduino

1. Open `arduino/warblet_open/warblet_open.ino` in the Arduino IDE.
2. Set `WIFI_NAME`, `WIFI_PASSWORD` and `BOARD_ID`. Until `BOARD_ID` is yours, the sketch sends nothing and says so on the Serial Monitor.
3. Pick your board and upload. Open the Serial Monitor at 115200 baud to watch each line and Warblet's answer.
4. Claim the board with Find device and tick **Open starter**.

The sketch needs no library beyond your board's own Wi-Fi one, and uses only `WiFi.begin()`, `WiFi.status()` and `WiFiClient` from it. The `#include` lines at the top pick that library; for a board they do not cover, include its Wi-Fi library there. The sketch has run on an ESP8266 board, built with the ESP8266 Arduino core 3.1.2.

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

On a platform with `<sys/socket.h>`, ESP-IDF among them, it builds as it is. On an lwIP project, build with `-DWARBLET_LWIP`, turn on `LWIP_SOCKET`, `LWIP_DNS`, `LWIP_SO_RCVTIMEO` and `LWIP_SO_SNDTIMEO`, and keep `LWIP_COMPAT_SOCKETS` on. Make sure your Wi-Fi joins again by itself after a drop: the file handles Warblet, not your network. Claim the board with Find device and tick **Open starter**.

### On ESP-IDF

The C file has run on an ESP32-C5 with ESP-IDF 5.5. Start from ESP-IDF's Wi-Fi station example, `examples/wifi/getting_started/station`:

1. Copy `warblet_open.c` into the example's `main` folder and add it to `main/CMakeLists.txt`. ESP-IDF has `<sys/socket.h>`, so the file builds as it is, without `-DWARBLET_LWIP`:

   ```cmake
   idf_component_register(SRCS "station_example_main.c" "warblet_open.c"
                          PRIV_REQUIRES esp_wifi nvs_flash
                          INCLUDE_DIRS ".")
   ```

2. Make the board join again after every drop. In `event_handler()` the example stops after `EXAMPLE_ESP_MAXIMUM_RETRY` tries: delete that limit, so a disconnect always calls `esp_wifi_connect()`. The example also writes your Wi-Fi password to the monitor: take it out of the two `ESP_LOGI` lines at the end of `wifi_init_sta()`.
3. Put the loop above in a function of its own, `static void warblet_task(void *arg)`, with `vTaskDelay(pdMS_TO_TICKS(10000))` as the delay, and start it at the end of `app_main()`: `xTaskCreate(warblet_task, "warblet", 6144, NULL, 5, NULL);`
4. Pick the chip first, because picking it starts a fresh configuration: the ESP32-C5 is a preview target in ESP-IDF 5.5, so `idf.py --preview set-target esp32c5`. Then type your network's name and password in `idf.py menuconfig` under Example Configuration, and build and flash. The monitor shows `1 hello -> 202`, then a new line about every 10 seconds.

### On an NXP MCUXpresso SDK project

The C file has run on an NXP RW612 with the MCUXpresso SDK's lwIP 2.2 and FreeRTOS, in a project that joins Wi-Fi with the SDK's connection manager (`wlan.h`):

1. Add `warblet_open.c` to your `source` folder and define `WARBLET_LWIP` for it, so it takes its sockets from lwIP and closes them with `lwip_close()`.
2. In `lwipopts.h`, make sure `LWIP_SOCKET`, `LWIP_DNS`, `LWIP_SO_RCVTIMEO` and `LWIP_SO_SNDTIMEO` are 1, and leave `LWIP_COMPAT_SOCKETS` on (lwIP's default): the file calls `socket()`, `connect()` and `getaddrinfo()` by those names.
3. Give it one task with 1,024 words of stack (it used under 450 on the RW612). Wait for an address, then run the loop above with `PRINTF` and `vTaskDelay(pdMS_TO_TICKS(10000))`:

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
4. Once the connection manager has been told to disconnect, it stays off the network until something calls `wlan_connect()` again. The file keeps trying Warblet every 10 seconds; joining Wi-Fi again is your firmware's job.

### Your own lines

The file also has `warblet_post()`, `warblet_get_command()` and `warblet_post_udp()`, so you can send your own lines. Its buffers are static, so call it from one task.

A board that can only send UDP can send its line with `warblet_post_udp()`, but nothing comes back over UDP, so it cannot say back what you type.

## The decoder

Setting the board up from the setup page, or claiming it with **Open starter** ticked, adds [decoder.star](decoder.star) for you. It turns `3 hello` into `count` 3 and `heard` "hello", so the device page shows the count and the text. A line that does not start with a count is stored with an error saying why.

## Step up: a token, then HTTPS

Open is for learning and quick experiments. When the board starts to matter:

1. **A token.** On the device page, open the **Security** setting, then **Keys and recovery**, and press **Rotate token**. The new token is shown once. Put it in the firmware as one more header line in the request each source builds in its `http()` helper: right after `Host: %s\r\n`, add `X-Chirp-Token: <token>\r\n`, so both requests carry it. Then raise the level. In the same Security setting, the line "This is the level for every device of …" names the board's spec as a link: open it, and under **Authentication target** set **Device authentication** to Token. Until a line arrives with the token, the Security setting shows **Send a valid token**. The first line that carries it moves the device to Token, and from then on a request without the token is refused. Other boards on the same spec keep reporting at Open until each one sends its own token.

   A board that cannot set a header can put `?token=<token>` at the end of both addresses instead. That costs more: an address is written into logs on its way, so the token sits in them, and over plain HTTP anyone on the network path reads it there, as they would read the header. With the token in the address, asking for a command must be a `POST`, which this demo already does.
2. **HTTPS.** A board that can do TLS can send the same two requests to `https.warbletiot.com` on port 443. Then nobody on the network can read them.

Signed messages and the other transports are in the other demos and at warbletiot.com/docs.

## Limits

- Anyone who knows the board's id can send lines as it and collect its commands. Over plain HTTP, anyone on the network path can read both.
- Give each board its own id. Two boards with one id are one device to Warblet: their counts interleave, and whichever asks first takes a command.
- Open allows about one line every 5 seconds per id.
- Lines sent before the board is claimed are seen, not stored. The count keeps going, so the first stored line can be `4 hello`.
- When the board restarts, the count starts again at 1 and what it heard goes back to `hello`.
- The board never sends a line twice. Between restarts, a gap in the counts is a line that never arrived.
- The Message box sends up to 256 characters. A command sent through the API can be longer, or not text at all, and one a routine sends can be empty: the board then says back nothing after the count. The Arduino sketch and the C file keep its first 1,024 bytes, up to any zero byte, and say them back as they are; a character the cut splits shows as � marks on the device page. The MicroPython files keep about the first 1,900 bytes, less up to 3 more so that it ends on a whole character; a command that is still not UTF-8 text leaves what the board heard unchanged.
- A Ctrl-C byte (0x03) on the USB console stops the MicroPython program, on purpose. Press RESET to start it again.

## Make it real

Replace the text the board sends with a reading of your own: change how `line` is built (in `main.py`, in `loop()` in the sketch, or in `warblet_open_step()` in C) and change `decoder.star` to match.

To drive a relay, a display or a buzzer, act where a command arrives: under `if status == 200:` in `main.py`, under `if (http("POST", path, "") == 200)` in the sketch, or where `warblet_get_command()` returns 1 in C. Compare what arrived (`heard`, or `command` in C) with the words you choose there. Acting on `heard` on every pass instead repeats the action every 10 seconds. When the reading matters, step up to a token.

## Security notes

`open_cfg.json` holds your Wi-Fi passwords as plain text in the board's flash, and the Arduino sketch holds them in the program. Anyone who can reach the USB port can read them, replace them with a `CHIRP+` push, or reflash the board: physical access is full access. Do not commit a copy of `open_cfg.json` or your filled-in constants; the repository's `.gitignore` excludes `open_cfg.json`.

## What the board prints

Each line ends with Warblet's answer:

| You see | It means |
|---|---|
| `-> 202 {"status":"unclaimed"}` | Warblet heard the board. Claim it to start storing its lines. |
| `-> 202 {"status":"accepted"}` | Stored. It is on the device page. |
| `-> 202` (Arduino, C) | Warblet has it: stored once the board is claimed, seen before that. |
| `-> 400` | The id has a character it cannot have. |
| `-> 401` | The device is at Token, and this line carried no token or an old one. Put the device's token in the firmware (see Step up), or take the device back to Open: set its spec's **Device authentication** to Open, then press **Lower to Open** in the device's Security setting. |
| `-> 429` | Too fast, or today's storage is used up. Open allows about one line every 5 seconds. Before a board is claimed, it also shares its network's allowance of new ids: about 24, then about one more every 2.5 minutes. Past that, a board whose id is new gets 429 and is not recorded, so Find device cannot find it yet; it keeps trying and gets in by itself when its turn comes. A claimed board is not counted. |
| `-> 502` | Warblet's plain-HTTP door could not reach the rest of Warblet. The next line tries again. |
| `-> -1` or `open: will try again` | No Wi-Fi, no name lookup, or no answer in time. Each read waits up to 15 seconds; on lwIP (ESP-IDF and NXP's MCUXpresso SDK) a connection nobody answers gives up after about 20 seconds. The board tries again 10 seconds later. |
| `open: not set up yet` | The board has no Wi-Fi or id yet. Connect it at warbletiot.com/flash. |
