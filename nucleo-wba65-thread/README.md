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

The ST source and libraries are not included. `make fetch` downloads the following repositories at the listed commits and places them in this directory (about 100 MB); it checks each commit and can be run again safely. It needs `git` and a network connection; on Windows, keep this repository near the drive root, because ST’s paths are long. The recipe is `tools/fetch_st.sh`.

| Repository | Commit | Destination |
|---|---|---|
| `STMicroelectronics/STM32CubeWBA` | `e455b860ceb52aaa0332a98f1e7a10bbd7014fc9` | Copy `Projects/NUCLEO-WBA65RI`, `Projects/Common`, `Utilities`, and `Drivers/CMSIS/Include` and `Core` to the same paths |
| `STMicroelectronics/stm32-mw-wpan` | `7e764835982f0a8b94fdd6b2b056d6e0afc4d1ba` | `Middlewares/ST/STM32_WPAN` |
| `STMicroelectronics/stm32wbaxx_hal_driver` | `c36d9aabc6068051ed27a3a683a6b4f35f04d815` | `Drivers/STM32WBAxx_HAL_Driver` |
| `STMicroelectronics/cmsis_device_wba` | `3373b40fa3b71ce114f58b170acd0f8833d42508` | `Drivers/CMSIS/Device/ST/STM32WBAxx` |
| `STMicroelectronics/stm32wbaxx-nucleo-bsp` | `653af5460b5fac428f4174dba6144518cf0a73a0` | `Drivers/BSP/STM32WBAxx_Nucleo` |
| `STMicroelectronics/stm32-mw-mbedtls` | `7ee7e20ec1460857446d2fa5628c1fd5ce6cd411` | `Middlewares/Third_Party/mbedtls` |

These versions belong to STM32CubeWBA v1.10.0. `make` says so if they are missing. The fetched files keep ST’s licenses; see [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).

Run from this demo’s directory:

```sh
make fetch
make
```

If needed, use `make GCC_DIR=/path/to/arm-none-eabi/bin`. The result is `build/thread_chirp.bin`. Keep the hard-float compiler settings: ST’s OpenThread, mbedTLS, and radio libraries use that ABI. `sources.mk` supplies the source list.

`ld/thread_chirp.ld` is modified from ST’s STM32CubeWBA linker script and stays under ST’s SLA0044 license in `ld/LICENSE.md`, not MIT: it may be used only with ST devices. See [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).

## Flash

The image loads at `0x08000000`. Connect ST-LINK USB-C (CN15). For a first install through ST-LINK, use STM32CubeProgrammer:

```sh
STM32_Programmer_CLI -c port=SWD mode=UR -d build/thread_chirp.bin 0x08000000 -v -rst
```

For browser flashing, connect user USB-C (CN9) as well. The board must be in its ROM DFU bootloader:

1. With this demo running, send `CHIRP~ dfu` on the ST-LINK serial port.
2. On a blank board, connect BOOT0 (CN1 pin 9 or CN3 pin 7) to 3V3 (CN3 pin 16), then press RESET. The board has no BOOT button.
3. Open https://warbletiot.com/flash in Chrome or Edge. Select `build/thread_chirp.bin` at `0x08000000`, write it, and leave DFU mode.
4. Remove the BOOT0 jumper, if used, and reset to run the app.

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

## What goes over the wire

The board resolves `udp.warbletiot.com` through the border router and sends UDP to port 7701. DNS64 and NAT64 let the IPv6 Thread network reach the IPv4 endpoint.

Without a signing key, the datagram is:

```text
CHIRP1 <hwid> <token> <two raw payload bytes>
```

With a signing key:

```text
CHIRP1 <hwid> <token>:<64 hex HMAC characters> <two raw payload bytes>
```

Spaces separate the text fields; the last two bytes are binary, with no trailing newline. The HMAC-SHA256 covers only those payload bytes. With no credential the token field is `-`.

The payload is an unsigned 16-bit number, high byte first, in tenths of a degree. `00 d6` means 21.4 °C. These are simulated readings.

This demo has no downlink, nonce, or replay protection. Thread protects its local radio link, but the UDP route to Warblet is not TLS and cannot satisfy `enforceTls`. Signing keys arrive through USB.

## Decoder

Paste [decoder.star](decoder.star) into the device spec’s decoder editor. It returns `temp_c` from the unsigned two-byte value.

## Make it real

Replace `next_demo_temperature_tenths_c()` in `chirp/src/chirp_frame.c` with a sensor reading in tenths of a degree.

Keep sensor initialization outside the frame builder. Change `chirp_frame_payload()` and `decoder.star` together for another format or negative readings. On another board, also adapt the serial, flash, bootloader, linker, and Thread platform code.

## Security notes

The stored configuration, including the claim code or token, the signing key, and the Thread network key or dataset, sits unencrypted in the last 8 KB flash page at `0x081FE000`. The console prints only short SHA-256 fingerprints of the secrets. ST-LINK, the ROM bootloader, and the `CHIRP~ dfu` command all give full access to the flash: physical access is full access. For a product, set readout protection (RDP), keep secrets in TrustZone-protected storage, remove or authenticate the DFU and provisioning commands, and give every device its own token and key. The UDP uplink is unencrypted once it leaves the Thread network.

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
