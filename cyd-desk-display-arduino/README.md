# Desk display (Arduino)

## What happens

Type a message in the Message box on your Warblet dashboard and it fills the screen of a Cheap Yellow Display on your desk. Tap the screen and Warblet stores that the message was seen. A routine that comes with this demo puts the fullest disk of each computer running the System monitor on the lower lines of the screen.

This is the [desk display](../cyd-desk-display/README.md) as an Arduino sketch, built on the [Warblet Arduino library](../arduino-libraries/README.md): the same screens, the same words and the same routine, in C++ you can change in the Arduino IDE. The library looks after Wi-Fi, the device credential, signed messages and the setup over USB; the sketch is the screen and what it shows.

## What you need

- A Cheap Yellow Display (Sunton ESP32-2432S028R) and a USB data cable.
- A 2.4 GHz Wi-Fi network and a Warblet account.
- For the disk lines: the System monitor running on a computer in the same account.
- To change the sketch: the Arduino IDE 2 or arduino-cli, with Espressif's Arduino core for ESP32 3.3.12.

## Quick start

Open [Set up a device](https://warbletiot.com/flash) in desktop Chrome or Edge, connect the board and select this demo. Follow the prompts to install it and send your network settings. Open the device page to see the result.

If the page cannot reach the board, hold **BOOT** (not RST) while you click **Connect over USB**, and let go once the page shows the chip. Do the same for **Install firmware and set up**: hold BOOT as you click it, and let go when the install shows a percentage. The install replaces the program that came on the board.

To change the sketch first, use the steps below.

## Build and upload from the Arduino IDE

1. In **Boards Manager**, install **esp32 by Espressif Systems**, version 3.3.12.
2. Copy [`arduino-libraries/Warblet`](../arduino-libraries/Warblet) into the `libraries` folder of your sketchbook, and restart the IDE.
3. Open `cyd-desk-display-arduino.ino`. The sketch has three tabs: the sketch, `screen.h` and `screen.cpp`.
4. Choose **Tools → Board → esp32 → ESP32-2432S028R CYD**, and the board's port.
5. Click **Upload**. If the upload stops at `Connecting...`, hold BOOT until it starts writing.
6. Close the Serial Monitor, then send your Wi-Fi and the device's settings at https://warbletiot.com/flash.

An upload keeps the board's setup (Wi-Fi, device id and key), unless **Tools → Erase All Flash Before Sketch Upload** is on. So after a change you upload again and the board carries on.

## Build with arduino-cli

From this directory:

```sh
arduino-cli core install esp32:esp32@3.3.12
arduino-cli compile --fqbn esp32:esp32:jczn_2432s028r --library ../arduino-libraries/Warblet --output-dir build .
arduino-cli upload --fqbn esp32:esp32:jczn_2432s028r --input-dir build -p <PORT> .
```

Replace `<PORT>` with the board's serial port, such as `COM5` or `/dev/ttyUSB0`. The default partition scheme fits the sketch, with room to spare.

The setup page writes one of two images:

| Image | Offset | Writes | When |
|---|---|---|---|
| `build/cyd-desk-display-arduino-full.bin` | `0x0` | The bootloader, the partition table and the sketch; it erases the board's setup | A first install |
| `build/cyd-desk-display-arduino-update.bin` | `0x10000` | The sketch only; the board keeps its setup | A board already running this demo |

To write the sketch alone by hand: `esptool --chip esp32 --port <PORT> write_flash 0x10000 build/cyd-desk-display-arduino.ino.bin`.

## The screen

The screen is used sideways, 320 × 240.

- **The top strip** says what the board is doing: **Connected** (green); **Reaching Warblet**, **Joining Wi-Fi**, **No Wi-Fi, retrying**, **Warblet: no answer**, **Warblet: slow down** (amber); **Warblet refused 401** and **Not set up** (red).
- **The middle** is the message, as big as it fits. A word is cut in two only when it is too long for the smallest letters. Before any message, it says "Type a message for" the board's id "on your Warblet dashboard".
- **The three lines below** are the routine's, one per computer. A fourth computer replaces the one heard from longest ago. "61% full: kitchen-pi /" shows as **61% kitchen-pi /**.
- **The bottom two lines** say how old the message is ("Message just now", "Message 3 min ago") and **Tap: I've seen it**, then **Seen: sending**, **Seen: sent** or, if the board was offline, **Seen: not sent yet**, which goes once it is back.

A tap anywhere sends one "seen" with the first 60 letters of the message; tapping again sends nothing new. The letters are MicroPython's 8x8 font, scaled up; letters outside plain English show as `?`.

**Not on an account** (amber) means Warblet does not know the board: its device was deleted, or it was never added to an account. The screen clears and says **Set me up at warbletiot.com/flash**, without the board's id, since anyone who knows the id of a board on no account could claim it. The board then stops: no reports, no checks for messages, Wi-Fi off, across restarts too, until a new setup over USB at warbletiot.com/flash.

If Wi-Fi is up but nothing has answered for 10 minutes, the board restarts itself; any answer from Warblet, a refusal too, counts. A restart clears the message; the routine sends the disk line again after the board's first report, if it is less than 10 minutes old.

## If the picture is wrong

Boards come with one of two screen chips: an ILI9341, or on most two-USB boards an ST7789. At start the sketch asks the screen which it is and sets itself up for that chip; a line on the Serial Monitor at start says what it found. Some two-USB boards cannot answer. They get the ST7789's settings, and the screen says **Screen chip guessed: see screen.h** in small grey letters.

The settings at the top of `screen.h` fix a wrong picture:

| Setting | Set it when |
|---|---|
| `INVERT = true` | The colours look like a photo negative: the black background is white. |
| `FLIP = true` | The picture is upside down. |
| `MIRROR = true` | The writing reads backwards. If it is then upside down, set `FLIP` too. |
| `SWAP_RED_BLUE = true` | Red shows as blue: the amber strip turns light blue. |
| `CHIP = "ILI9341"` or `"ST7789"` | The guess was wrong, or to skip the question. |

Change the line, upload again, and the board keeps its setup.

## Facts

| | |
|---|---|
| **Transport** | HTTPS to `https.warbletiot.com`: a report every 30 seconds with the Wi-Fi signal (`{"wifi_dbm": -52}`, the first after a start tagged `hello`), and a check for messages every 5 seconds, taking up to 10 at a time |
| **Security cap** | L2 (Signed), with a key; L1 (Token) without one |
| **Toolchain** | Arduino core for ESP32 3.3.12, board `esp32:esp32:jczn_2432s028r`, and the Warblet library in [`arduino-libraries/`](../arduino-libraries/README.md) |
| **Routine** | [desk-display-disk.star](desk-display-disk.star), listed as **Desk display: show the fullest disk**; the same file as the MicroPython desk display's |
| **Decoder** | [decoder.star](decoder.star): each key of the board's JSON becomes a field, `wifi_dbm` and `seen` |

USB setup at warbletiot.com/flash stores the Wi-Fi network (up to eight, tried in order), the board's id, a one-time setup code that becomes its token, and for Signed its signing key, in the board's settings storage. The library's [reference](../arduino-libraries/Warblet/docs/REFERENCE.md) lists every call the sketch uses.

## Make it real

Change what the screen shows in `showMessage()` and `showInfo()` in `cyd-desk-display-arduino.ino`, and point the routine at any reading from your other boards. The colours and the layout are constants at the top of the sketch. The same sketch is the library's DeskDisplay example.
