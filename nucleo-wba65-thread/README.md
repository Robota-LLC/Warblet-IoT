# NUCLEO-WBA65RI — simulated temperature over Thread

## What happens

The board joins a Thread network and sends a two-byte simulated temperature to Warblet every 30 seconds. The reading starts at 20.1 °C, rises to 40.0 °C, then repeats from 20.0 °C. Credentials are sent through USB and saved in flash.

## What you need

- An ST NUCLEO-WBA65RI.
- A USB-C data cable for ST-LINK (CN15), which supplies power and serial data.
- A second USB-C data cable for user USB (CN9) if flashing through DFU.
- A Thread border router with internet access, DNS64/NAT64, and its active network dataset.
- GNU Make, `arm-none-eabi-gcc`, the ST dependencies below, and a Warblet account.

## Connections and settings

The console is USART1 through ST-LINK: PB12 TX and PA8 RX, AF7, at 115200 baud. No external sensor is needed for the simulated reading.

`CHIRP_REPORT_INTERVAL_MS`, `CHIRP_REPORT_DEFAULT_HOST`, and `CHIRP_REPORT_UDP_PORT` are in `chirp/src/chirp_report.c`. The serial adapter in `chirp/src/board_serial.c` uses DMA to receive full command lines while the radio is active.

## Build

Run `make fetch` to download the pinned STM32CubeWBA 1.10.0 dependencies, then build. You need Git 2.25 or newer, a POSIX shell (such as Git Bash on Windows), GNU Make and the Arm compiler. Keep the checkout path short on Windows.

Exact revisions and download paths are in [tools/fetch_st.sh](tools/fetch_st.sh). The fetched files retain ST’s licenses; see [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).

Run from this demo’s directory:

```sh
make fetch
make
```

If needed, use `make GCC_DIR=/path/to/arm-none-eabi/bin`. The result is `build/thread_chirp.bin`. Keep the hard-float compiler settings: ST’s OpenThread, mbedTLS, and radio libraries use that ABI. `sources.mk` supplies the source list.

`ld/thread_chirp.ld` is modified from ST’s STM32CubeWBA linker script and stays under ST’s SLA0044 license in `ld/LICENSE.md`, not MIT: it may be used only with ST devices. See [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).

`make test` checks the firmware’s signing code against known HMAC values on your computer. It needs a host C compiler such as `gcc`, not the Arm compiler.

## Flash

The image loads at `0x08000000`. Connect ST-LINK USB-C (CN15). For a first install through ST-LINK, use STM32CubeProgrammer:

```sh
STM32_Programmer_CLI -c port=SWD mode=UR -d build/thread_chirp.bin 0x08000000 -v -rst
```

For browser flashing, connect user USB-C (CN9) as well. The board must be in its ROM DFU bootloader:

1. Enter DFU either by sending `CHIRP~ dfu` on the ST-LINK serial port while this demo is running, or by connecting BOOT0 (CN1 pin 9 or CN3 pin 7) to 3V3 (CN3 pin 16) and pressing RESET. The board has no BOOT button.
2. Open https://warbletiot.com/flash in Chrome or Edge. Select `build/thread_chirp.bin` at `0x08000000`, write it, and leave DFU mode.
3. Remove the BOOT0 jumper, if used, and reset to run the app.

On Windows, browser access to the ROM bootloader may require a WinUSB driver. If the boot-pin route does not work, use the ST-LINK command above. These images require a compatible non-secure flash configuration; check `TZEN=0` and `BOOT_LOCK=0` in STM32CubeProgrammer before flashing.

The last 8 KB flash page at `0x081FE000` stores settings. An app write that leaves that page alone preserves them; a full-chip erase does not.

## Set up Thread

1. Get the active operational dataset from your border router. It is a hex string containing the Thread network settings. Keep it private.
2. Open https://warbletiot.com/flash and connect to the ST-LINK serial port.
3. Send the dataset, device ID, token, and optional signing key.
4. Open the device page and check for reports.

For manual setup, open the serial port at 115200 baud, 8 data bits, no parity, and 1 stop bit. Replace every example value and send the push on one line:

```text
CHIRP?
CHIRP+ {"thread":{"dataset":"YOUR_DATASET_HEX"},"hwid":"YOUR_DEVICE_ID","token":"YOUR_DEVICE_TOKEN"}
```

The board answers `CHIRP= ok` when it saves the config. The optional `key` is a base64-encoded 32-byte signing key. Use `claim` or `token`, not both. Later pushes merge with saved fields; a key-only push needs a network and credential already stored.

The same console accepts OpenThread commands such as `state`. A connected node has a role such as `child` or `router`. `dataset active -x` shows the network dataset, including private credentials; do not share its output.

## Data and commands

The board resolves `udp.warbletiot.com` through the border router and sends UDP to port 7701. DNS64 and NAT64 let the IPv6 Thread network reach the IPv4 endpoint.

Without a signing key, the datagram is:

```text
CHIRP1 <hwid> <token> <two raw payload bytes>
```

With a signing key:

```text
CHIRP1 <hwid> <token>:<64 hex HMAC characters> <two raw payload bytes>
```

Spaces separate the text fields; the last two bytes are binary, with no trailing newline. With no credential the token field is `-`.

The HMAC-SHA256 covers `"\ntag=\n" + payload`: an empty nonce line, an empty tag line, then the binary payload. Include both lines when porting the signing code; they are not sent in the datagram.

The payload is an unsigned 16-bit number, high byte first, in tenths of a degree. `00 d6` means 21.4 °C. These are simulated readings.

This demo has no downlink, nonce, or replay protection. Thread protects its local radio link, but the UDP route to Warblet is not TLS and cannot satisfy `enforceTls`. Signing keys arrive through USB.

## Decoder

Browser setup adds the demo spec. For manual setup, paste [decoder.star](decoder.star) into your device spec’s decoder editor. It returns `temp_c` from the unsigned two-byte value.

## Make it real

Replace `next_demo_temperature_tenths_c()` in `chirp/src/chirp_frame.c` with a sensor reading in tenths of a degree.

Keep sensor initialization outside the frame builder. Change `chirp_frame_payload()` and `decoder.star` together for another format or negative readings. On another board, also adapt the serial, flash, bootloader, linker, and Thread platform code.

## Security notes

Settings and credentials are stored unencrypted at `0x081FE000`. ST-LINK, the ROM bootloader and USB setup allow reading or replacing them. Protect debug and setup access and use separate device credentials before deployment.

Thread encrypts the local radio link; the UDP route beyond it is unencrypted. Signing does not encrypt the payload.

## Troubleshooting

| Problem | Check |
|---|---|
| Role stays `detached` | Check the dataset, channel, and router range. |
| DNS fails | Check the border router’s DNS64/NAT64 and internet connection. |
| `missing-field` | A first push needs the network and credential. |
| `overrun` | Close competing serial tools and resend the whole line. |
| `too-long` | Keep the command below the 4160-byte line-buffer size. |
| No serial answer | Use CN15 at 115200 baud. |
| Stored key is unusable | Send a valid signing key; confirm acceptance on the device page. |
| Reports sent but no reading appears | UDP has no delivery acknowledgement. Check the device ID, credential, and rejected-message count. |
