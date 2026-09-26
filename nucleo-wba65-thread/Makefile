# GNU Make build for NUCLEO-WBA65RI Thread firmware.
# Uses STM32CubeWBA v1.10.0 settings and libraries.
# Output: build/thread_chirp.bin at 0x08000000. See README.md for dependencies.

TARGET  := thread_chirp
BUILD   := build
APP     := Projects/NUCLEO-WBA65RI/Applications/Thread/Thread_Cli_Cmd_FTD

# Use GNU Arm tools from PATH, or set GCC_DIR to their bin directory.
# CROSS_COMPILE selects the executable prefix.
CROSS_COMPILE ?= arm-none-eabi-
GCC_DIR       ?=
TOOL          := $(if $(GCC_DIR),$(GCC_DIR)/,)$(CROSS_COMPILE)

CC      := $(TOOL)gcc
AS      := $(TOOL)gcc -x assembler-with-cpp
OBJCOPY := $(TOOL)objcopy
SIZE    := $(TOOL)size

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
  -DOPENTHREAD_PROJECT_CORE_CONFIG_FILE='<stm32wba-openthread-ftd-config.h>'

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
WRAPS    := -Wl,--wrap=otPlatUartReceived -Wl,--wrap=otCliInit

LDFLAGS  := $(CPU) -T$(LDSCRIPT) --specs=nano.specs --specs=nosys.specs \
            -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map,--cref \
            -Wl,--print-memory-usage $(WRAPS)

# ---- objects -----------------------------------------------------------------
OBJS := $(addprefix $(BUILD)/,$(SRCS:.c=.o)) $(addprefix $(BUILD)/,$(ASRCS:.s=.o))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean size test fetch
all: $(BUILD)/$(TARGET).bin size

# ---- host unit tests ---------------------------------------------------------
# Hardware-free sources are compiled natively. serialtest runs board_serial.c
# against stub ST headers and a model of ST's CLI output buffer.
HOSTCC ?= gcc
ifeq ($(OS),Windows_NT)
  HOSTEXE := .exe
else
  HOSTEXE :=
endif

test:
	@mkdir -p $(BUILD)
	@"$(HOSTCC)" -std=c11 -Wall -Wextra -Ichirp/inc -o $(BUILD)/valtest$(HOSTEXE) \
	   chirp/test/valtest.c chirp/src/json.c chirp/src/chirpval.c chirp/src/chirp_ds.c \
	   chirp/src/chirp_frame.c
	@$(BUILD)/valtest$(HOSTEXE)
	@"$(HOSTCC)" -std=c11 -Wall -Wextra -Ichirp/test/stub -Ichirp/inc \
	   -o $(BUILD)/serialtest$(HOSTEXE) chirp/test/serialtest.c
	@$(BUILD)/serialtest$(HOSTEXE)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	@$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "  AS   $<"
	@$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
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
