# Desk display

## What happens

Type a message in the Message box on your Warblet dashboard and it fills the screen of a Cheap Yellow Display on your desk. Tap the screen and Warblet stores that the message was seen. A routine that comes with this demo puts the fullest disk of a computer running the System monitor on the lower lines of the screen.

The full guide is coming.

## What you need

- A Cheap Yellow Display (Sunton ESP32-2432S028R) and a USB data cable.
- A 2.4 GHz Wi-Fi network and a Warblet account.
- For the disk line: the System monitor running on a computer in the same account.

## Facts

| | |
|---|---|
| **Transport** | HTTPS to `https.warbletiot.com`: a report every 30 seconds, and a check for messages every 5 seconds |
| **Security cap** | L2 (Signed), with a key; L1 (Token) without one |
| **Runtime** | [MicroPython 1.28.0 ESP32_GENERIC](https://micropython.org/resources/firmware/ESP32_GENERIC-20260406-v1.28.0.bin) at `0x1000`; [demo.json](demo.json) also lists its SHA-256 checksum |
| **Routine** | [desk-display-disk.star](desk-display-disk.star), listed as **Desk display: show the fullest disk** |

USB setup stores your Wi-Fi network and a one-time setup code (`claim`) that becomes the board's token. An `aps` list can hold up to eight networks, tried in order.

## Make it real

Change what the screen shows in `show_message()` and `show_info()` in `main.py`, and point the routine at any reading from your other boards.
