# NUCLEO-WBA65RI — simulated temperature through a USB relay

## What happens

Every 30 seconds the board prints an HTTP request to its serial port. A Python relay on your computer sends it to Warblet. The two-byte reading is simulated: it starts at 20.1 °C, rises to 40.0 °C, then repeats from 20.0 °C. This firmware does not use the board’s radio.

## What you need

- An ST NUCLEO-WBA65RI.
- A USB-C data cable for ST-LINK (CN15), which supplies power and serial data.
- A second USB-C data cable for user USB (CN9) if flashing through DFU.
- A computer with internet access, Python, and the `pyserial` package.
- GNU Make, `arm-none-eabi-gcc`, and a Warblet account.

## Pins and settings

`include/board.h` names the board connections: LD1 is PD8, active low; B1 is PC13, active low. USART1 uses PB12 for TX and PA8 for RX, both AF7, through ST-LINK. The console runs at 115200 baud.

`SEND_PERIOD_MS` and `DEFAULT_HOST` are at the top of `src/chirp_send.c`. No sensor wiring is needed for the simulated reading.

## Build

Run from this directory with GNU Make and the Arm compiler on your PATH:

```sh
make
```

If needed, set the compiler directory:

```sh
make GCC_DIR=/path/to/arm-none-eabi/bin
```

The result is `build/chirpwba-demo.bin`. This is C firmware with register headers; it does not require an RTOS or a vendor SDK.

The headers in `include/cmsis/` (Arm) and `include/st/` (STMicroelectronics) are unmodified vendor files under Apache-2.0, with the license text beside them. The MIT license does not cover them; see [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).

## Flash

The image loads at `0x08000000`. Connect ST-LINK USB-C (CN15). For a first install through ST-LINK, use STM32CubeProgrammer:

```sh
STM32_Programmer_CLI -c port=SWD mode=UR -d build/chirpwba-demo.bin 0x08000000 -v -rst
```

For browser flashing, connect user USB-C (CN9) as well. The board must be in its ROM DFU bootloader:

1. With this demo running, send `CHIRP~ dfu` on the ST-LINK serial port.
2. On a blank board, connect BOOT0 (CN1 pin 9 or CN3 pin 7) to 3V3 (CN3 pin 16), then press RESET. The board has no BOOT button.
3. Open https://warbletiot.com/flash in Chrome or Edge. Select `build/chirpwba-demo.bin` at `0x08000000`, write it, and leave DFU mode.
4. Remove the BOOT0 jumper, if used, and reset to run the app.

On Windows, browser access to the ROM bootloader may require a WinUSB driver. If the boot-pin route does not work, use the ST-LINK command above. These images require a compatible non-secure flash configuration; check `TZEN=0` and `BOOT_LOCK=0` in STM32CubeProgrammer before flashing.

The last 8 KB flash page at `0x081FE000` stores settings. An app write that leaves that page alone preserves them; a full-chip erase does not.

## Set up the device

Open https://warbletiot.com/flash and use the ST-LINK serial port to send the device settings. This demo needs a device credential, but no Wi-Fi or Thread settings. Your computer supplies the internet connection.

For manual setup, open the ST-LINK serial port at 115200 baud, 8 data bits, no parity, and 1 stop bit. Replace the values below:

```text
CHIRP?
CHIRP+ {"hwid":"YOUR_DEVICE_ID","token":"YOUR_DEVICE_TOKEN"}
```

`CHIRP= ok` means the settings were saved. An optional `key` is a base64-encoded 32-byte signing key. Use `claim` or `token`, not both. Later pushes merge with stored settings, so a key-only push works after the device has a credential.

## Run the computer relay

Install the serial package with `python -m pip install pyserial`. Save this code as `relay.py` on your computer. Replace `YOUR_SERIAL_PORT` with the ST-LINK port. Close the browser’s serial connection before running `python relay.py`.

```python
import socket
import serial

PORT = "YOUR_SERIAL_PORT"
HOST = "http.warbletiot.com"
headers = []

with serial.Serial(PORT, 115200, timeout=1) as port:
    while True:
        line = port.readline().decode("utf-8", "replace").rstrip("\r\n")
        if not line.startswith("CHIRP^ "):
            continue
        text = line[7:]
        if not text.startswith("body "):
            headers.append(text)
            continue
        request = "\r\n".join(headers) + "\r\nConnection: close\r\n\r\n"
        payload = bytes.fromhex(text[5:])
        headers = []
        try:
            with socket.create_connection((HOST, 80), timeout=10) as connection:
                connection.sendall(request.encode() + payload)
                print(connection.recv(256).split(b"\r\n")[0].decode())
        except OSError as error:
            print("Relay connection failed:", error)
```

Keep the relay running to receive reports. If you set a custom `host` on the board, change the relay’s `HOST` too. The serial requests contain the device credential; keep captured logs private.

## What goes over the wire

The relay removes the `CHIRP^ ` prefix from each header line and converts the `body` line from hex to bytes. It sends `POST /ingest/{hwid}` to `http.warbletiot.com:80`.

The body is an unsigned 16-bit number, high byte first, in tenths of a degree. `00 c9` means 20.1 °C. A token uses `X-Chirp-Token`; a claim uses `X-Chirp-Claim`. With a signing key, `X-Chirp-Signature` holds a hex HMAC-SHA256 of the two body bytes.

This demo has no nonce or replay protection. HTTP is unencrypted, so it cannot satisfy `enforceTls`. It does not receive downlink commands.

## Decoder

Paste [decoder.star](decoder.star) into the device spec’s decoder editor. It returns `temp_c` from the unsigned two-byte value. The dashboard reading is simulated until you attach a sensor and change the reading function.

## Make it real

Replace `next_tenths()` in `src/chirp_send.c` with your sensor reading in tenths of a degree.

For negative readings, change the unsigned packing and decoder to a signed format. For a different payload size, also update `Content-Length` and the serial body formatter in `print_http_request()`. Keep hardware setup separate from the request formatting when adding a sensor.

## Security notes

The stored configuration, including the claim code or token, the signing key, and the Thread network key or dataset, sits unencrypted in the last 8 KB flash page at `0x081FE000`. The console prints only short SHA-256 fingerprints of the secrets. ST-LINK, the ROM bootloader, and the `CHIRP~ dfu` command all give full access to the flash: physical access is full access. For a product, set readout protection (RDP), keep secrets in TrustZone-protected storage, remove or authenticate the DFU and provisioning commands, and give every device its own token and key. The relayed HTTP uplink is unencrypted between the relay and Warblet.

## Troubleshooting

| Problem | Check |
|---|---|
| No console output | Use CN15, the ST-LINK port, at 115200 baud. |
| No relay output | Wait 30 seconds after boot and close other apps using the serial port. |
| `missing-field` | A first push needs a token or claim; a signing key alone is not enough. |
| `bad-json` for a key | Supply a base64-encoded 32-byte key. |
| Relay connection failed | Check the computer’s internet connection, DNS, and `HOST`. |
| HTTP 401 | Check the device credential and signing key. |
| No LED heartbeat | Check that the app is running and that the image was written to `0x08000000`. |
