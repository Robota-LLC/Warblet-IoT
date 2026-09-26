# Third-party notices

The MIT license in this repository's `LICENSE` file covers Robota's own code. The files below contain other projects' work, which keeps its own license; the MIT license does not apply to that work.

| Files | Component | Copyright | License | License text |
|---|---|---|---|---|
| `nucleo-wba65-baremetal/include/cmsis/` | CMSIS-Core(M) headers from `STMicroelectronics/cmsis-core` at `afc5ca6af0a4` (release 6.3.0), unmodified | Arm Limited | Apache-2.0 | [LICENSE.md](nucleo-wba65-baremetal/include/cmsis/LICENSE.md) |
| `nucleo-wba65-baremetal/include/st/` | STM32WBAxx device headers from `STMicroelectronics/cmsis-device-wba` at `3373b40fa3b7`, unmodified | STMicroelectronics | Apache-2.0 | [LICENSE.md](nucleo-wba65-baremetal/include/st/LICENSE.md) |
| `nucleo-wba65-thread/ld/thread_chirp.ld` | Linker script modified from STM32CubeWBA v1.10.0 `STM32WBA65RIVX_FLASH.ld` (flash length and the config-page symbols) | STMicroelectronics | ST SLA0044 | [LICENSE.md](nucleo-wba65-thread/ld/LICENSE.md) |

SLA0044 allows redistribution when the notice and conditions are kept, but only for use with STMicroelectronics devices, and it forbids placing the file under an open-source license. Keep `thread_chirp.ld` under SLA0044 when you copy it.

Runtimes, SDKs, and components that a demo's build or setup steps download are not included here. Their own licenses apply: MicroPython and its libraries and, for the compiled demos, the components listed below, whose license texts are in `licenses/`.

## Fetched at build time: the Thread demo

`nucleo-wba65-thread/tools/fetch_st.sh` (`make fetch`) downloads these STMicroelectronics repositories at the commits STM32CubeWBA v1.10.0 pins. They are not part of this repository and the MIT license does not cover them; each keeps its own license, and the built image `thread_chirp.bin` contains code from every row.

| Fetched into | Component | License | Text |
|---|---|---|---|
| `Drivers/STM32WBAxx_HAL_Driver/` | STM32WBAxx HAL and LL drivers (`stm32wbaxx_hal_driver`) | BSD-3-Clause, STMicroelectronics | `licenses/BSD-3-Clause-STMicroelectronics.txt` |
| `Drivers/BSP/STM32WBAxx_Nucleo/` | Nucleo board support (`stm32wbaxx-nucleo-bsp`) | BSD-3-Clause, STMicroelectronics | `licenses/BSD-3-Clause-STMicroelectronics.txt` |
| `Drivers/CMSIS/Device/ST/STM32WBAxx/` | Device headers and startup code (`cmsis_device_wba`) | Apache-2.0, STMicroelectronics | `licenses/Apache-2.0.txt` |
| `Drivers/CMSIS/Include/`, `Drivers/CMSIS/Core/` | CMSIS-Core(M) headers, through `STM32CubeWBA`; `make fetch` places `Drivers/CMSIS/LICENSE.txt` beside them | Apache-2.0, Arm Limited | `licenses/Apache-2.0.txt` |
| `Utilities/` | Sequencer, timer server, low-power manager, trace and misc utilities, through `STM32CubeWBA` | BSD-3-Clause, STMicroelectronics | `licenses/BSD-3-Clause-STMicroelectronics.txt` |
| `Projects/Common/`, `Projects/NUCLEO-WBA65RI/` | The application code the demo builds on: `Thread_Cli_Cmd_FTD` and the shared WPAN modules and interfaces, through `STM32CubeWBA` | ST SLA0044 | `licenses/SLA0044.txt` |
| `Middlewares/ST/STM32_WPAN/thread/openthread/platform/`, `config/`, `common/` | ST's OpenThread port: the platform layer the demo compiles (`stm32-mw-wpan`) | ST SLA0044 | `licenses/SLA0044.txt` |
| `Middlewares/ST/STM32_WPAN/thread/openthread/openthread_lib/` | The prebuilt OpenThread FTD stack archive, built by STMicroelectronics from OpenThread (`stm32-mw-wpan`) | BSD-3-Clause, The OpenThread Authors | `licenses/BSD-3-Clause-OpenThread.txt` |
| `Middlewares/ST/STM32_WPAN/link_layer/`, `mac_802_15_4/` | The 802.15.4 link layer and MAC, including the prebuilt link-layer archive (`stm32-mw-wpan`) | ST SLA0044, Synopsys and STMicroelectronics | `licenses/SLA0044.txt` |
| `Middlewares/Third_Party/mbedtls/` | mbedTLS headers (`stm32-mw-mbedtls`); the prebuilt mbedTLS archive comes with the WPAN middleware | Apache-2.0 | `licenses/Apache-2.0.txt` |
| linked from the toolchain | The C library (newlib-nano) and the GCC runtime library (libgcc) of GNU Arm Embedded / GNU Tools for STM32 | newlib: BSD-style, several copyright holders; libgcc: GPL-3.0 with the GCC Runtime Library Exception | `licenses/COPYING.NEWLIB.txt`, `licenses/GCC-Runtime-Library-Exception.txt` |

The license texts are also in the fetched directories (`LICENSE.md`, `LICENSE` or `LICENSE.txt`; the WPAN middleware's `LICENSE.md` is a bill of materials with the SLA0044 text). SLA0044 covers ST's application code, its OpenThread port, the link layer and the MAC, so it covers the built image too: distribute `thread_chirp.bin` only for use on STMicroelectronics devices, keep ST's copyright notice, conditions and disclaimer with it, and do not place it under an open-source license. The BSD-3-Clause and Apache-2.0 parts require their notices to travel with the image in binary form.

## Built into the ESP-IDF images: FireBeetle C5 and the two camera demos

`firebeetle-c5`, `freenove-wrover-cam` and `freenove-wrover-cam-mqtts` build with ESP-IDF v5.5. Their images (`merged-binary.bin`, which includes the bootloader and the partition table, and the application image) contain these components, read off the linker map of each build; none is part of this repository.

| Component | License | Text |
|---|---|---|
| ESP-IDF: the framework, its drivers, the bootloader, and the prebuilt Wi-Fi, PHY and coexistence libraries (Espressif Systems) | Apache-2.0 | `licenses/Apache-2.0.txt` |
| FreeRTOS kernel (Amazon.com, Inc. and contributors) | MIT | `licenses/MIT-FreeRTOS-Kernel.txt` |
| Mbed TLS, under its Apache-2.0 option | Apache-2.0 | `licenses/Apache-2.0.txt` |
| lwIP (Swedish Institute of Computer Science and contributors) | BSD-3-Clause | `licenses/BSD-3-Clause-lwIP.txt` |
| newlib, the C library of the Espressif toolchain | BSD-style, several copyright holders | `licenses/COPYING.NEWLIB.txt` |
| wpa_supplicant (Jouni Malinen and contributors) | BSD-3-Clause | `licenses/BSD-3-Clause-wpa_supplicant.txt` |
| cJSON (Dave Gamble and contributors) | MIT | `licenses/MIT-cJSON.txt` |
| http-parser (Joyent, Inc. and contributors), in the camera demos' HTTP client | MIT | `licenses/MIT-http_parser.txt` |
| esp-mqtt (Espressif Systems), in the MQTTS camera demo | Apache-2.0 | `licenses/Apache-2.0.txt` |
| esp32-camera (Espressif Systems), in the two camera demos | Apache-2.0 | `licenses/Apache-2.0.txt` |
| TLSF, the two-level segregated fit allocator behind the heap (Matthew Conte), in the two camera images; the FireBeetle C5 uses the copy in its ROM | BSD-3-Clause | `licenses/BSD-3-Clause-TLSF.txt` |
| Cadence Design Systems' Xtensa code in the two camera images: the Xtensa HAL (`libxt_hal.a`), the exception vectors, context and interrupt handling, and the FreeRTOS Xtensa port | MIT | `licenses/MIT-Cadence-Xtensa.txt` |
| libgcc and, where linked, libstdc++: the GCC runtime libraries of the Espressif toolchain | GPL-3.0 with the GCC Runtime Library Exception | `licenses/GCC-Runtime-Library-Exception.txt` |

## Bundled license texts

`licenses/` holds the texts named above, and `licenses/demos.json` says which of them each compiled demo's image contains. The catalog publisher puts `LICENSE`, this file and those texts into every demo's download bundle, so the notices travel with the image.
