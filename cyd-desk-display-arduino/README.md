# Desk display (Arduino)

## What happens

Type a message in the Message box on your Warblet dashboard and it fills the screen of a Cheap Yellow Display on your desk. Tap the screen and Warblet stores that the message was seen. A routine that comes with this demo puts the fullest disk of each computer running the System monitor on the lower lines of the screen.

This is the [desk display](../cyd-desk-display/README.md) as an Arduino sketch, built on the [Warblet Arduino library](../arduino-libraries/README.md): the same screens, the same words and the same routine, in C++ you can change in the Arduino IDE. The library looks after Wi-Fi, the device credential, signed messages and the setup over USB; the sketch is the screen and what it shows. Where it differs from the MicroPython desk display is listed below.

## What you need

- A Cheap Yellow Display (Sunton ESP32-2432S028R) and a USB data cable.
- A 2.4 GHz Wi-Fi network and a Warblet account.
- Desktop Chrome or Edge for browser setup.
- For the disk lines: the System monitor running on a computer in the same account.

## Quick start

1. Open [Set up the Desk display (Arduino)](https://warbletiot.com/flash?demo=demo-cyd-desk-display-arduino) in Chrome or Edge. Plug in the board and choose **Connect over USB**.
2. Choose **Install a demo** if offered, and check that the selected demo is **Desk display (Arduino)**. Enter your Wi-Fi settings and follow the page to install and set up the board.
3. Open its Warblet device page. Type `Dinner is ready` in **Message**, then send it.
4. When the words appear, tap the screen. **Seen: sent** means the board has sent the tap to Warblet; the device page shows a `seen` field with the start of your message.

Installing on a new board or replacing another demo writes the full image: it replaces the program and erases the board's setup, then the page sends the settings again. Reinstalling on a board that identifies itself as this demo writes only the sketch and keeps its setup. Save a backup of anything you want to keep.

To send settings without replacing the sketch, choose **Keep current firmware** when the page recognizes this demo. It writes no firmware image; the settings selected on the page still change.

Close other serial tools while the browser uses the board; unplug other USB serial boards if their names in the chooser are hard to tell apart.

<details>
<summary>If the board needs BOOT and RST</summary>

Select **Skip automatic reset**, then click Connect or Install. Wait for the page to open the USB connection and show **Put the board in install mode** before pressing the board's buttons:

1. Hold **BOOT**.
2. Press and release **RST**.
3. Let go of **BOOT**.

Click **Continue**. Repeat the three steps only when the page shows this prompt again. If the page asks you to restart the board, keep BOOT released and press RST.

</details>

Browser setup supplies the decoder, the **Message** box and **Clear** button, and adds and enables **Desk display: show the fullest disk**. **Clear** removes the message and leaves the disk lines. Run the [System monitor](../system-monitor/README.md) on each computer you want to show, using the same Warblet account; the routine finds displays set up from either desk display demo, with no display IDs to enter.

## Build and upload from the Arduino IDE

To change the sketch, use Arduino IDE 2 or arduino-cli with Espressif's Arduino core for ESP32 3.3.12.

1. In **Boards Manager**, install **esp32 by Espressif Systems**, version 3.3.12.
2. Copy [`arduino-libraries/Warblet`](../arduino-libraries/Warblet) into the `libraries` folder of your sketchbook, and restart the IDE.
3. Open `cyd-desk-display-arduino.ino`. The sketch has three tabs: the sketch, `screen.h` and `screen.cpp`.
4. Choose **Tools → Board → esp32 → ESP32-2432S028R CYD**, and the board's port.
5. Click **Upload**. If the upload stops at `Connecting...`, hold BOOT until it starts writing.
6. Close the Serial Monitor, then send your Wi-Fi and the device's settings at [Set up a device](https://warbletiot.com/flash). If the sketch still identifies itself as this demo, choose **Keep current firmware** to preserve your edits.

An upload keeps the board's setup (Wi-Fi, device id and key), unless **Tools → Erase All Flash Before Sketch Upload** is on. So after a change you upload again and the board carries on.

## Build with arduino-cli

From this directory:

```sh
arduino-cli core install esp32:esp32@3.3.12
arduino-cli compile --fqbn esp32:esp32:jczn_2432s028r --library ../arduino-libraries/Warblet --output-dir build .
arduino-cli upload --fqbn esp32:esp32:jczn_2432s028r --input-dir build -p <PORT> .
```

Replace `<PORT>` with the board's serial port, such as `COM5` or `/dev/ttyUSB0`. The default partition scheme fits the sketch, with room to spare.

The browser installer chooses between two published images. Neither install erases the whole chip; the full image overwrites the area that holds the board's setup.

| Image | Offset | Writes | When |
|---|---|---|---|
| `build/cyd-desk-display-arduino-full.bin` | `0x0` | The bootloader, the partition table and the sketch; it erases the board's setup | A board that does not answer the setup probe, or identifies itself as another demo |
| `build/cyd-desk-display-arduino-update.bin` | `0x10000` | The sketch only; the board keeps its setup | A board already running this demo |

To write the sketch alone by hand: `esptool --chip esp32 --port <PORT> write_flash 0x10000 build/cyd-desk-display-arduino.ino.bin`.

## The screen

The screen is used sideways, 320 × 240.

- **The top strip** says what the board is doing: **Connected** (green); **Reaching Warblet**, **Joining Wi-Fi**, **No Wi-Fi, retrying**, **Warblet: no answer** (nothing answered, or a fault on Warblet's side), **Warblet: slow down** (Warblet asking it to send less often) (amber); **Warblet refused** with the code it answered, such as 401 for a refused token, and **Not set up** (red).
- **The middle** is the message, as big as it fits. A word is cut in two only when it is too long for the smallest letters. Before any message, it says "Type a message for" the board's id "on your Warblet dashboard".
- **The three lines below** are the routine's, one per computer. A fourth computer replaces the one heard from longest ago. "61% full: kitchen-pi /" shows as **61% kitchen-pi /**.
- **The bottom two lines** say how old the message is ("Message just now", "Message 3 min ago") and **Tap: I've seen it**, then **Seen: sending**, **Seen: sent** or, if the board was offline, **Seen: not sent yet**, which goes once it is back.

A tap anywhere sends one "seen" with the first 60 letters of the message; tapping again sends nothing new. The letters are MicroPython's 8x8 font, scaled up; letters outside plain English show as `?`.

**Not on an account** (amber) means Warblet does not know the board: its device was deleted, or it was never added to an account. The screen clears and says **Set me up at warbletiot.com/flash**. Nothing the board sends would reach an account, so it stops: no reports, no checks for messages, and it leaves its Wi-Fi network (the radio stays on), across restarts too, until a new setup over USB at warbletiot.com/flash.

If Wi-Fi is up but nothing has answered for 10 minutes, the board restarts itself; any answer from Warblet, a refusal too, counts. A network that blocks network time (NTP) counts as no answer too: the board cannot send without the date, so the strip says **Warblet: no answer** and the same restart applies. A restart clears the message; the routine sends the disk line again after the board's first report, if it is less than 10 minutes old.

A new setup over USB that changes the board's id or its token restarts it as well, so it starts afresh under the new setup, says hello, and shows nothing the old one had. New Wi-Fi alone is used without a restart, and the message stays.

Each message the board sends is said on the Serial Monitor with the memory left, for example `desk: report stored, 172000 bytes free, largest block 110000`.

## If the picture is wrong

Boards come with one of two screen chips: an ILI9341, or on most two-USB boards an ST7789. At start the sketch asks the screen which it is and sets itself up for that chip; a line on the Serial Monitor at start says what it found. Some two-USB boards cannot answer. They get the ST7789's settings, and the screen says **Screen chip guessed: see screen.h** in small grey letters.

The sketch drives the screen itself, with no graphics library: libraries such as TFT_eSPI fix the chip when the sketch is built, and this one asks the screen at start, and draws the same pixels as the MicroPython desk display.

The settings at the top of `screen.h` fix a wrong picture:

| Setting | Set it when |
|---|---|
| `INVERT = true` | The colours look like a photo negative: the black background is white. |
| `FLIP = true` | The picture is upside down. |
| `MIRROR = true` | The writing reads backwards. If it is then upside down, set `FLIP` too. |
| `SWAP_RED_BLUE = true` | Red shows as blue: the amber strip turns light blue. |
| `CHIP = "ILI9341"` or `"ST7789"` | The guess was wrong, or to skip the question. Any other name stops the build with a message. |

Change the line, upload again, and the board keeps its setup.

## Where it differs from the MicroPython desk display

- **A refusal shows its code.** Warblet turning a message or a check away with 403 or 404 shows **Warblet refused** and the code; the MicroPython display says **Warblet: no answer** for those. A fault on Warblet's side (a 5xx) is **Warblet: no answer** on both.
- **New Wi-Fi keeps the message.** A setup push of Wi-Fi alone is used at once; the MicroPython display restarts after every push. A push with a new id or token restarts both.
- **No Stopped screen.** There is no prompt to stop the sketch at, so it never shows **Stopped**.
- **Longer messages.** A message up to 8,192 bytes is shown; the MicroPython display does not show one over 1,024.
- **Waits after a refusal.** After Warblet refuses a message or does not answer, the board waits 20 s to 10 minutes before it sends the next. A tap in that time says **Seen: not sent yet** and is sent when the wait ends. Checks for messages slow down the same way: up to a minute when nothing answers, up to 10 minutes after refusals.
- **A wrong picture needs a rebuild.** The `screen.h` settings are part of the sketch, so changing one means uploading it again from the Arduino IDE or arduino-cli; the MicroPython display keeps them in a file on the board.
- **The guessed-chip notice names screen.h.** A screen that does not say which chip it is gets **Screen chip guessed: see screen.h**, the file with this sketch's settings; the MicroPython display's says `screen.py`.

## Facts

| | |
|---|---|
| **Transport** | HTTPS to `https.warbletiot.com`: a report every 30 seconds with the Wi-Fi signal (`{"wifi_dbm": -52}`, the first after a start tagged `hello`), and a check for messages every 5 seconds, taking up to 10 at a time |
| **Security cap** | L2 (Signed), with a key; L1 (Token) without one. Signed is supported by the library but untested with this sketch on a CYD. |
| **Toolchain** | Arduino core for ESP32 3.3.12, board `esp32:esp32:jczn_2432s028r`, and the Warblet library in [`arduino-libraries/`](../arduino-libraries/README.md) |
| **Routine** | [desk-display-disk.star](desk-display-disk.star), listed as **Desk display: show the fullest disk**; the same file as the MicroPython desk display's |
| **Decoder** | [decoder.star](decoder.star): each key of the board's JSON becomes a field, `wifi_dbm` and `seen` |

USB setup at warbletiot.com/flash stores the Wi-Fi network (up to eight, tried in order), the board's id, a one-time setup code that becomes its token, and for Signed its signing key, in the board's settings storage. The library's [reference](../arduino-libraries/Warblet/docs/REFERENCE.md) lists every call the sketch uses.

## Make it real

Change what the screen shows in `showMessage()` and `showInfo()` in `cyd-desk-display-arduino.ino`, and point the routine at any reading from your other boards. The colours and the layout are constants at the top of the sketch. The same sketch is the library's DeskDisplay example.

For your own application, remove both `const char* DEMO = "demo-cyd-desk-display-arduino";` and `board.demo(DEMO);`. Otherwise the setup page treats your sketch as this demo and offers its spec, decoder and dashboard; **Keep current firmware** preserves your sketch, while installing the demo replaces it.

With those lines removed, the page sets up a board of your own. Add [decoder.star](decoder.star) to its spec and a **Message** box and **Clear** button (text `clear`) to its dashboard, or adapt them for your application. A device set up this way is not automatically included in the disk routine; adapt the routine to send to it.
