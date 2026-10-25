# Desk display

## What happens

Type a message on your Warblet dashboard and it fills the screen of a Cheap Yellow Display. Tap the screen to say you have seen it. A routine adds the fullest disk from computers running the System monitor in your account.

Warblet supplies the Message box, stores reports and taps, and connects the display to your other devices through the routine. Leave a note across the room or put a useful reading on your desk.

## What you need

- A Cheap Yellow Display (Sunton ESP32-2432S028R) and a USB data cable.
- A 2.4 GHz Wi-Fi network and a Warblet account.
- Desktop Chrome or Edge for browser setup.
- For disk readings: the System monitor running on computers in the same account.

## Quick start

1. Open [Set up the Desk display](https://warbletiot.com/flash?demo=demo-cyd-desk-display) in Chrome or Edge. Plug in the board and choose **Connect over USB**.
2. Check that the selected demo is **Desk display**. Enter your Wi-Fi settings and follow the page to install and set up the board.
3. Open its Warblet device page. Type `Dinner is ready` in **Message**, then send it.
4. When the words appear, tap the screen. **Seen: sent** means the board has sent the tap to Warblet; the device page shows a `seen` field with the start of your message.

Installing the MicroPython runtime erases the board's existing program, files and settings, including its factory demo. Updating only the demo files replaces files with the same names; setup writes the settings selected on the page. Save a backup of anything you want to keep.

Close other serial tools while the browser uses the board; unplug other USB serial boards if their names in the chooser are hard to tell apart.

<details>
<summary>If the board needs BOOT and RST</summary>

Select **Skip automatic reset**, then click Connect or Install. Wait for the page to open the USB connection and show **Put the board in install mode** before pressing the board's buttons:

1. Hold **BOOT**.
2. Press and release **RST**.
3. Let go of **BOOT**.

Click **Continue**. Repeat the three steps only when the page shows this prompt again. If the page asks you to restart the board, keep BOOT released and press RST.

</details>

## Using the display

Messages can contain up to 256 characters; the letters shrink to fit. The board checks every five seconds, with extra time for the connection and drawing, so allow a few seconds after sending.

Tap anywhere to acknowledge the current message. The board sends its first 60 characters in `seen`, once per message. Hold your finger briefly: a very short tap can be missed while the board is talking to Warblet, and pressing the screen while moving the board also counts.

**Clear** removes the message and leaves the disk lines. The footer shows how long the current message has been on screen. If several messages are waiting, the newest one ends up on screen.

### Your computers' disks

Browser setup adds and enables **Desk display: show the fullest disk**. Run the [System monitor](../system-monitor/README.md) on each computer you want to show, using the same Warblet account. There are no display IDs to enter in the routine.

A line such as `61% kitchen-pi /` means the fullest disk on the computer named `kitchen-pi` is 61% used. Each computer gets one row, up to three rows; a fourth replaces the computer heard from longest ago. Names and disk paths are shortened to fit the screen.

The routine finds both MicroPython and Arduino Desk displays installed through Warblet in the same account. After a restart, the most recent disk line returns if it is less than ten minutes old; other rows fill as computer reports arrive. Displays set up entirely by hand are not included automatically.

## Staying connected

The top strip shows the connection: joining Wi-Fi, reaching Warblet, then **Connected** when Warblet answers.

If Wi-Fi drops, the board retries and keeps the message on screen. A tap made while offline shows **Seen: not sent yet** and waits for a connection. A new message or a restart clears that pending tap.

If Wi-Fi stays connected but Warblet gives no answer for about ten minutes, the board restarts and clears the message. This can happen during an internet outage even with the router on. Send the message again when the connection returns.

**Not set up** means the board needs USB setup. **Not on an account** means Warblet no longer recognizes its setup; the board stops communicating and needs USB setup again. Deleting a device from the dashboard does not erase its Wi-Fi settings or keys from the board.

## Facts

| | |
|---|---|
| **Screen** | 320 × 240, landscape; ILI9341, with an ST7789 driver that is untested on hardware |
| **Transport** | HTTPS to `https.warbletiot.com`; a report about every 30 seconds and a message check every 5 seconds |
| **Security cap** | L2 (Signed), with a signing key; L1 (Token) without one |
| **Runtime** | MicroPython 1.28.0 ESP32_GENERIC; this demo runs as Python source |
| **History** | Up to 7 days, subject to your plan and available storage |
| **Routine** | [desk-display-disk.star](desk-display-disk.star), listed as **Desk display: show the fullest disk** |

The font shows basic Latin letters, numbers and punctuation. Accents and emoji appear as `?`. The demo has no clock or on-screen buttons: a tap anywhere means “seen.”

## Pins and settings

The screen and touch panel are built in; no extra wiring is needed. Settings at the top of [screen.py](screen.py) let you correct the picture or use another compatible display.

<details>
<summary>Correct the picture or change the screen wiring</summary>

| What you see | Setting in `screen.py` |
|---|---|
| Writing reads backwards | `MIRROR = True`; also set `FLIP = True` if it is then upside down |
| Picture is upside down | `FLIP = True` |
| Red and blue are swapped | `SWAP_RED_BLUE = True` |
| Colours look like a photo negative | `INVERT = True` |
| “Screen chip guessed: see screen.py” and the picture is wrong | Set `CHIP = "ILI9341"` instead of `None`; `"ST7789"` is the other choice |

The demo asks the screen which chip it uses. If the screen does not answer, it uses ST7789 settings and shows the “Screen chip guessed” note. This fallback is untested on hardware; check the picture and use the settings above if it is wrong.

With Python and `mpremote` on your computer, close the browser's USB connection and copy the board's file out. Replace `<PORT>` with its serial port, such as `COM5` or `/dev/ttyUSB0`.

```sh
python -m pip install mpremote
mpremote connect <PORT> fs cp :screen.py screen.py
```

Edit the setting, copy the file back, then restart:

```sh
mpremote connect <PORT> fs cp screen.py :screen.py
mpremote connect <PORT> reset
```

Changing this file keeps the Wi-Fi and device setup. For a different board, change the pin assignments too:

| Connection | GPIO |
|---|---|
| Screen SPI clock / data out / data in | 14 / 13 / 12 |
| Screen select / command-data / backlight | 15 / 2 / 21 |
| Touch SPI clock / data out / data in | 25 / 32 / 39 |
| Touch select / press signal | 33 / 36 |

The backlight is on when high. The screen resets with the board. The touch controller is an XPT2046.

</details>

## Build by hand

There is no compile step. Browser setup supplies the dashboard controls and routine; the steps below let you install your own source.

<details>
<summary>Install the runtime and demo files from a terminal</summary>

Install the computer tools, using esptool 5 or later for these command names:

```sh
python -m pip install --upgrade esptool mpremote
```

Download [MicroPython 1.28.0 ESP32_GENERIC](https://micropython.org/resources/firmware/ESP32_GENERIC-20260406-v1.28.0.bin). [demo.json](demo.json) lists its SHA-256 checksum. Run from this demo's folder and replace `<PORT>` with the board's serial port.

Run the following command first. If esptool waits for a connection, hold BOOT, press and release RST, then release BOOT. The command erases the whole flash and writes the runtime at `0x1000`:

```sh
python -m esptool --chip esp32 --port <PORT> --before no-reset write-flash --erase-all 0x1000 ESP32_GENERIC-20260406-v1.28.0.bin
```

Press RST with BOOT released if the board does not restart. Then copy the five app files and reset:

```sh
mpremote connect <PORT> fs cp main.py screen.py chirp_prov.py chirp_send.py chirp_sign.py :
mpremote connect <PORT> reset
```

See the [esptool command reference](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/basic-commands.html) and [mpremote guide](https://docs.micropython.org/en/latest/reference/mpremote.html) for those tools.

</details>

## Set up the connection

Use [USB setup](https://warbletiot.com/flash?demo=demo-cyd-desk-display) after installing your edited files, and select **Keep current firmware** before sending settings to preserve your edits.

<details>
<summary>Manual connection settings</summary>

For terminal setup at 115200 baud, follow the [ESP32 connection guide](../esp32-generic/README.md#set-up-the-connection). This demo stores the same settings in `chirp_cfg.json`:

- `ssid` and `pass`: Wi-Fi name and password; an `aps` list holds up to eight networks, tried in order.
- `hwid`: the device ID.
- `claim` or `token`: a setup code or device token; send one, not both.
- `key`: the base64-encoded 32-byte signing key.
- `host`: the server address, normally `https.warbletiot.com`.

A later push can contain only changed fields. Sending blank settings does not erase the setup.

</details>

## Data and commands

Reports are JSON: `{"wifi_dbm":-52}` gives Wi-Fi signal strength; `{"seen":"Dinner is ready"}` records a tap. The first report after startup has the tag `hello`; tap reports have the tag `seen`. [decoder.star](decoder.star) turns each JSON property into a Warblet field.

Commands are UTF-8 text. `clear` clears the message, `info:61% full: kitchen-pi /` updates a lower row, and other text becomes the main message. The exact word `clear` and the prefix `info:` are reserved, including in the Message box.

Keep commands sent by a routine or API within 1,024 bytes. Larger commands are ignored by the board even if Warblet shows them as delivered. Check the screen to confirm what appeared; a delivery status is not proof it was displayed.

## Security notes

`chirp_cfg.json` contains the Wi-Fi password, device token and signing key without encryption. Anyone with access to the USB console can read or change them. Keep the file out of Git and use separate credentials for each device.

To use a deleted device again, run USB setup with a new setup code. A previously used ID cannot be claimed by name or lowered to Open. Before giving the board away, erase its flash to remove your Wi-Fi settings and keys.

<details>
<summary>Erase the board before giving it away</summary>

1. Close the browser's USB connection and other serial tools. Unplug other serial boards so you can identify this one's port.
2. Hold **BOOT** while plugging the board into USB and starting the command below.
3. With esptool 5 or later installed, replace `<PORT>` with this board's port and run:

```sh
python -m esptool --chip esp32 --port <PORT> --before no-reset erase-flash
```

If esptool waits for a connection, press and release RST while still holding BOOT. Release BOOT once the erase starts, and wait for the tool to confirm it succeeded. This removes the program, Wi-Fi settings and credentials; install the demo again to reuse it.

</details>

## Make it real

Change `show_message()` and `show_info()` in [main.py](main.py) to choose what fills the screen; the colours and report intervals are near the top. Change [desk-display-disk.star](desk-display-disk.star) to send a reading from another device, such as temperature or a tank level. Keep the command format and display code in step.

For on-screen buttons, add touch coordinates and calibrate them for your panel. This demo only detects a press, which lets the whole screen act as the acknowledgment button.
