# GNU Make build for NUCLEO-WBA65RI Thread firmware.
# Uses STM32CubeWBA v1.10.0 settings and libraries.
# Output: build/thread_chirp.bin at 0x08000000. See README.md for dependencies.

TARGET  := thread_chirp
BUILD   := build
APP     := Projects/NUCLEO-WBA65RI/Applications/Thread/Thread_Cli_Cmd_FTD

# fw=: the release in which this demo's firmware last changed. The board
# announces it, and demo.json's firmware.fw says the same (make test checks).
FW_VERSION := v1.6.0

# Use GNU Arm tools from PATH, or set GCC_DIR to their bin directory.
# CROSS_COMPILE selects the executable prefix.
CROSS_COMPILE ?= arm-none-eabi-
GCC_DIR       ?=
TOOL          := $(if $(GCC_DIR),$(GCC_DIR)/,)$(CROSS_COMPILE)

CC      := $(TOOL)gcc
AS      := $(TOOL)gcc -x assembler-with-cpp
OBJCOPY := $(TOOL)objcopy
SIZE    := $(TOOL)size
NM      := $(TOOL)nm

# One clear line beats make's "No such file or directory" when the toolchain is
# missing. `clean` and `test` need no cross compiler.
ifeq ($(filter clean test fetch,$(MAKECMDGOALS)),)
  ifeq ($(shell "$(CC)" -dumpversion 2>/dev/null),)
    $(error $(CC) not found -- install arm-none-eabi-gcc and put it on PATH, or build with GCC_DIR=/path/to/toolchain/bin)
  endif
endif

# ---- ST sources --------------------------------------------------------------
# Fetched by `make fetch` (tools/fetch_st.sh) at the commits it pins, never
# committed. One clear line beats forty "No such file or directory" errors.
ST_MARKER := Drivers/STM32WBAxx_HAL_Driver/Inc/stm32wbaxx_hal.h
ifeq ($(filter clean test fetch,$(MAKECMDGOALS)),)
  ifeq ($(wildcard $(ST_MARKER)),)
    $(error ST sources are not here yet: run 'make fetch' first. It downloads about 100 MB from github.com/STMicroelectronics)
  endif
endif

# ---- cpu ---------------------------------------------------------------------
# Hard-float ABI is required: ST's prebuilt archives are built that way.
CPU := -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard

# ---- defines (.cproject, Release configuration) ------------------------------
DEFS := \
  -DUSE_HAL_DRIVER -DUSE_FULL_LL_DRIVER -DUSE_FULL_ASSERT -DSTM32WBA65xx \
  -DOPENTHREAD_FTD -DMAC -DMAC_LAYER \
  -DDISABLE_RFTS_EXT_EVNT_HNDLR=1 -DPHY_40nm_3_40_a \
  -DOPENTHREAD_CONFIG_FILE='<stm32wba-openthread-ftd-config.h>' \
  -DOPENTHREAD_PROJECT_CORE_CONFIG_FILE='<stm32wba-openthread-ftd-config.h>' \
  -DCHIRP_FW='"$(FW_VERSION)"'

# ---- include paths (.cproject, verbatim, rebased to repo root) ---------------
INCS := \
  -I$(APP)/Core/Inc \
  -I$(APP)/STM32_WPAN/App \
  -I$(APP)/STM32_WPAN/Target \
  -I$(APP)/System/Config \
  -I$(APP)/System/Config/CRC_Ctrl \
  -I$(APP)/System/Config/Debug_GPIO \
  -I$(APP)/System/Config/Log \
  -I$(APP)/System/Config/LowPower \
  -I$(APP)/System/Interfaces \
  -IDrivers/BSP/STM32WBAxx_Nucleo \
  -IDrivers/CMSIS/Device/ST/STM32WBAxx/Include \
  -IDrivers/CMSIS/Include \
  -IDrivers/STM32WBAxx_HAL_Driver/Inc \
  -IDrivers/STM32WBAxx_HAL_Driver/Inc/Legacy \
  -IMiddlewares/ST/STM32_WPAN \
  -IMiddlewares/ST/STM32_WPAN/link_layer/ll_cmd_lib/config/thread \
  -IMiddlewares/ST/STM32_WPAN/link_layer/ll_cmd_lib/inc \
  -IMiddlewares/ST/STM32_WPAN/link_layer/ll_cmd_lib/inc/_40nm_reg_files \
  -IMiddlewares/ST/STM32_WPAN/link_layer/ll_cmd_lib/inc/ot_inc \
  -IMiddlewares/ST/STM32_WPAN/link_layer/ll_cmd_lib/porting \
  -IMiddlewares/ST/STM32_WPAN/link_layer/ll_cmd_lib/src/shrd_utils/inc \
  -IMiddlewares/ST/STM32_WPAN/link_layer/ll_sys/inc \
  -IMiddlewares/ST/STM32_WPAN/thread/openthread/common \
  -IMiddlewares/ST/STM32_WPAN/thread/openthread/config \
  -IMiddlewares/ST/STM32_WPAN/thread/openthread/platform \
  -IMiddlewares/ST/STM32_WPAN/thread/openthread/stack/include \
  -IMiddlewares/ST/STM32_WPAN/thread/openthread/stack/include/openthread \
  -IMiddlewares/ST/STM32_WPAN/thread/openthread/stack/src/core \
  -IMiddlewares/ST/STM32_WPAN/thread/openthread/stack/src/core/config \
  -IMiddlewares/ST/STM32_WPAN/thread/openthread/stack/third_party/mbedtls \
  -IMiddlewares/Third_Party/mbedtls/include \
  -IMiddlewares/Third_Party/mbedtls/include/mbedtls \
  -IProjects/Common/WPAN/Interfaces \
  -IProjects/Common/WPAN/Modules \
  -IProjects/Common/WPAN/Modules/BasicAES \
  -IProjects/Common/WPAN/Modules/Log \
  -IProjects/Common/WPAN/Modules/RTDebug \
  -IProjects/Common/WPAN/Modules/SerialCmdInterpreter \
  -IUtilities/lpm/tiny_lpm \
  -IUtilities/misc \
  -IUtilities/sequencer \
  -IUtilities/tim_serv \
  -IUtilities/trace/adv_trace \
  -Ichirp/inc

# Use the generated linked-resource list, plus startup and syscall sources.
include sources.mk

ASRCS := $(ASRCS_PROJ) \
  $(APP)/STM32CubeIDE/Application/User/Startup/startup_stm32wba65rivx.s

SRCS += \
  $(APP)/STM32CubeIDE/Application/User/Core/syscalls.c \
  $(APP)/STM32CubeIDE/Application/User/Core/sysmem.c

# Warblet modules for config, serial setup, reporting, and signing.
# Port serial, flash, DFU, and platform integration when changing boards.
SRCS += \
  chirp/src/json.c \
  chirp/src/chirpval.c \
  chirp/src/chirp_ds.c \
  chirp/src/chirp_frame.c \
  chirp/src/chirp_hwid.c \
  chirp/src/flashcfg.c \
  chirp/src/board_serial.c \
  chirp/src/chirp_prov.c \
  chirp/src/chirp_sign.c \
  chirp/src/chirp_report.c \
  chirp/src/chirp_dfu.c

# ---- prebuilt ST archives ----------------------------------------------------
# OpenThread, mbedTLS and the 802.15.4 link layer ship precompiled.
OTLIB := Middlewares/ST/STM32_WPAN/thread/openthread/openthread_lib
LLLIB := Middlewares/ST/STM32_WPAN/link_layer/ll_cmd_lib/lib
LIBS  := \
  $(OTLIB)/stm32wba_ot_ftd_lib.a \
  $(OTLIB)/stm32wba_mbedtls_ftd_lib.a \
  $(LLLIB)/LinkLayer_802_15_4_V3_0.a

LDSCRIPT := ld/thread_chirp.ld

# ---- flags -------------------------------------------------------------------
OPT      := -Os
WARN     := -Wall -Wextra -Wno-unused-parameter
CFLAGS   := $(CPU) $(DEFS) $(INCS) $(OPT) $(WARN) \
            -ffunction-sections -fdata-sections -fstack-usage \
            -MMD -MP -std=gnu11
ASFLAGS  := $(CPU) $(OPT) -ffunction-sections -fdata-sections

# Wrap CLI initialization to register setup and reporting tasks.
# Wrap UART input to dispatch complete lines without editing ST sources.
# Wrap the HAL's 1-byte receive so that ST's uart.c never arms it on the CLI
# UART: its receive callback echoes a line before any of it reaches us, so
# ST must never receive a byte (chirp/src/board_serial.c).
WRAPS    := -Wl,--wrap=otPlatUartReceived -Wl,--wrap=otCliInit -Wl,--wrap=HAL_UART_Receive_IT

# chirp/src/flashcfg.c's NMI_Handler catches a flash ECC error in the config
# page and passes every other NMI to ST's, which is built under another name.
# (The startup file defines NMI_Handler too, so a --wrap would not reach it.)
$(BUILD)/$(APP)/Core/Src/stm32wbaxx_it.o: CFLAGS += -DNMI_Handler=st_NMI_Handler

LDFLAGS  := $(CPU) -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs \
            -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map,--cref \
            -Wl,--print-memory-usage $(WRAPS)

# ---- objects -----------------------------------------------------------------
OBJS := $(addprefix $(BUILD)/,$(SRCS:.c=.o)) $(addprefix $(BUILD)/,$(ASRCS:.s=.o))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean size test fetch
all: $(BUILD)/$(TARGET).bin size

# ---- host unit tests ---------------------------------------------------------
# Hardware-free sources are compiled natively. valtest builds chirp_sign.c
# against a host SHA-256 in chirp/test/ that stands in for ST's mbedTLS, and
# flashcfg.c against a flash of its own that can fail or lose power at any
# step. serialtest runs board_serial.c against stub ST headers and a model of
# ST's CLI output buffer; hwidtest holds the announce to the id the reports
# use and to demo.json; boottest runs the real start, chirp_prov.c,
# chirp_report.c and board_serial.c together on stand-ins for OpenThread and
# ST's scheduler (chirp/test/stub). VECTORS is the path to Warblet's shared CHIRP-PROV
# vectors (chirp-prov-v1.8.json); without it those checks are skipped.
# echotest runs ST's own uart.c, as make fetch places it, with board_serial.c,
# linked with the firmware's wraps: no line before the takeover is echoed.
ST_UART_C := Middlewares/ST/STM32_WPAN/thread/openthread/platform/uart.c
HOSTCC  ?= gcc
VECTORS ?=
ifeq ($(OS),Windows_NT)
  HOSTEXE := .exe
else
  HOSTEXE :=
endif

test:
	@mkdir -p $(BUILD)
	@"$(HOSTCC)" -std=c11 -Wall -Wextra -Ichirp/inc -Ichirp/test -Ichirp/test/stub -DCFG_FAKE_FLASH \
	   -DCHIRP_FW='"$(FW_VERSION)"' -o $(BUILD)/valtest$(HOSTEXE) \
	   chirp/test/valtest.c chirp/src/json.c chirp/src/chirpval.c chirp/src/chirp_ds.c \
	   chirp/src/chirp_frame.c chirp/src/chirp_sign.c chirp/src/flashcfg.c chirp/src/chirp_hwid.c \
	   chirp/test/sha256_host.c
	@$(BUILD)/valtest$(HOSTEXE) $(VECTORS)
	@"$(HOSTCC)" -std=c11 -Wall -Wextra -Ichirp/test/stub -Ichirp/inc \
	   -o $(BUILD)/serialtest$(HOSTEXE) chirp/test/serialtest.c
	@$(BUILD)/serialtest$(HOSTEXE)
	@"$(HOSTCC)" -std=c11 -Wall -Wextra -Ichirp/inc -DCHIRP_FW='"$(FW_VERSION)"' \
	   -o $(BUILD)/hwidtest$(HOSTEXE) chirp/test/hwidtest.c chirp/src/chirp_hwid.c chirp/src/json.c
	@$(BUILD)/hwidtest$(HOSTEXE)
	@"$(HOSTCC)" -std=c11 -Wall -Wextra -Ichirp/inc -Ichirp/test -Ichirp/test/stub -DCFG_FAKE_FLASH \
	   -DCHIRP_FW='"$(FW_VERSION)"' -o $(BUILD)/boottest$(HOSTEXE) \
	   chirp/test/boottest.c chirp/src/chirp_prov.c chirp/src/chirp_report.c chirp/src/board_serial.c \
	   chirp/src/chirp_hwid.c chirp/src/chirpval.c chirp/src/json.c chirp/src/flashcfg.c \
	   chirp/src/chirp_ds.c chirp/src/chirp_frame.c chirp/src/chirp_sign.c chirp/test/sha256_host.c
	@$(BUILD)/boottest$(HOSTEXE)
	@if [ -f $(ST_UART_C) ]; then \
	  "$(HOSTCC)" -std=gnu11 -w -Ichirp/test/stub_st -Ichirp/test/stub -c $(ST_UART_C) -o $(BUILD)/st_uart_host.o && \
	  "$(HOSTCC)" -std=c11 -Wall -Wextra -Ichirp/test -Ichirp/test/stub -Ichirp/inc -o $(BUILD)/echotest$(HOSTEXE) \
	    chirp/test/echotest.c chirp/test/echo_hal.c chirp/src/board_serial.c $(BUILD)/st_uart_host.o \
	    -Wl,--wrap=HAL_UART_Receive_IT -Wl,--wrap=otPlatUartReceived && \
	  $(BUILD)/echotest$(HOSTEXE); \
	else echo "SKIP  echotest: ST's uart.c is not here yet (make fetch)"; fi

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	@$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "  AS   $<"
	@$(AS) -c $(ASFLAGS) $< -o $@

# ST never receives on the CLI UART, so it never echoes: the wrap above
# refuses the one call ST's uart.c arms its receive with. This fails the build
# if that stops holding:
#   - a file make fetch placed that the image is built from is not the one
#     the receive audit read: every ST source compiled (the startup file
#     too), the three archives, and every ST header the compiler opened, for
#     ST's objects and ours (the .d files list them), each pinned by sha256
#     in tools/st_receive_audit.sha256 (tools/st_audit.sh). A register write
#     or an inlined LL call leaves no symbol, so a changed, missing or
#     unlisted file fails until a person has audited it again and run
#     make st-audit-write;
#   - ST's uart.o no longer arms its receive through that call;
#   - an object or library other than board_serial.o and the HAL's own UART
#     files (where these calls live and call each other; what they compile
#     from is pinned above) reaches the UART's receive by any other call.
ST_AUDIT  := tools/st_receive_audit.sha256
ST_SRCS   := $(filter-out chirp/%,$(SRCS) $(ASRCS))
ST_UART_O := $(BUILD)/Middlewares/ST/STM32_WPAN/thread/openthread/platform/uart.o
RX_OWN    := $(BUILD)/chirp/src/board_serial.o $(BUILD)/Drivers/STM32WBAxx_HAL_Driver/Src/stm32wbaxx_hal_uart.o \
             $(BUILD)/Drivers/STM32WBAxx_HAL_Driver/Src/stm32wbaxx_hal_uart_ex.o
RX_OTHER  := HAL_UART_Receive|HAL_UART_Receive_DMA|HAL_UARTEx_ReceiveToIdle|HAL_UARTEx_ReceiveToIdle_IT|HAL_UARTEx_ReceiveToIdle_DMA|UART_Start_Receive_IT|UART_Start_Receive_DMA
$(BUILD)/st_receive.ok: $(OBJS) $(LIBS) $(ST_AUDIT) tools/st_audit.sh
	@sh tools/st_audit.sh check $(ST_AUDIT) -- $(ST_SRCS) $(LIBS) -- $(DEPS)
	@$(NM) -u $(ST_UART_O) | grep -qE ' U HAL_UART_Receive_IT$$' || \
	  { echo "ST's uart.o no longer arms its receive with HAL_UART_Receive_IT, which the wrap refuses: ST could echo what it receives"; exit 1; }
	@other=$$($(NM) -u -A $(filter-out $(RX_OWN),$(OBJS)) $(LIBS) 2>/dev/null | grep -E ' U ($(RX_OTHER))$$'); \
	  if [ -n "$$other" ]; then echo "the UART's receive is reached by a call the wrap does not refuse:"; echo "$$other"; exit 1; fi
	@echo "  OK   ST's receive on the CLI UART is never armed"
	@touch $@

# Only after a person has audited what changed: the ST files this image is
# built from, each with its sha256, into tools/st_receive_audit.sha256.
.PHONY: st-audit-write
st-audit-write: $(OBJS)
	@sh tools/st_audit.sh write $(ST_AUDIT) -- $(ST_SRCS) $(LIBS) -- $(DEPS)

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT) $(BUILD)/st_receive.ok
	@echo "  LD   $@"
	@$(CC) $(OBJS) $(LDFLAGS) -Wl,--start-group $(LIBS) -lc -lm -Wl,--end-group -o $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@$(OBJCOPY) -O binary -S $< $@
	@echo "  BIN  $@"

size: $(BUILD)/$(TARGET).elf
	@$(SIZE) $<

clean:
	rm -rf $(BUILD)

# ---- ST sources, fetched -----------------------------------------------------
fetch:
	@sh tools/fetch_st.sh

-include $(DEPS)
