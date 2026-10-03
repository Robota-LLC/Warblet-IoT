# Build bare-metal STM32WBA65RI firmware with GNU Arm tools.
# Use GCC_DIR for a compiler outside PATH, or override CROSS_COMPILE.

CROSS_COMPILE ?= arm-none-eabi-
GCC_DIR       ?=
TOOL          := $(if $(GCC_DIR),$(GCC_DIR)/,)$(CROSS_COMPILE)

CC      = $(TOOL)gcc
OBJCOPY = $(TOOL)objcopy
SIZE    = $(TOOL)size

# One clear line beats make's "No such file or directory" when the toolchain is
# missing. `clean` and `test` need no cross compiler.
ifeq ($(filter clean test,$(MAKECMDGOALS)),)
  ifeq ($(shell "$(CC)" -dumpversion 2>/dev/null),)
    $(error $(CC) not found -- install arm-none-eabi-gcc and put it on PATH, or build with GCC_DIR=/path/to/toolchain/bin)
  endif
endif

TARGET  = chirpwba-demo
BUILD   = build

SRCS = \
  src/startup_stm32wba65xx.c \
  src/system.c \
  src/uart.c \
  src/json.c \
  src/chirpval.c \
  src/flashcfg.c \
  src/sha256.c \
  src/chirp_sign.c \
  src/chirp_prov.c \
  src/chirp_send.c \
  src/dfu.c \
  src/main.c

OBJS = $(addprefix $(BUILD)/,$(notdir $(SRCS:.c=.o)))
vpath %.c src

INCLUDES = -Iinclude -Iinclude/st -Iinclude/cmsis

# Cortex-M33, no FPU use (soft float keeps the image free of FP context).
CPU = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft

DEFS = -DSTM32WBA65xx

CFLAGS = $(CPU) $(DEFS) $(INCLUDES) -std=c11 -Os -g3 \
         -Wall -Wextra -Wshadow -Wundef -Wdouble-promotion \
         -ffunction-sections -fdata-sections -fno-common \
         -fno-builtin -fno-strict-aliasing

LDSCRIPT = ld/stm32wba65ri.ld
LDFLAGS  = $(CPU) -T$(LDSCRIPT) -nostdlib -nostartfiles \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage

all: $(BUILD)/$(TARGET).bin

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "--- $(TARGET).bin ---"
	@ls -l $@

# ---- host unit tests ---------------------------------------------------------
# These four files touch no hardware, so the firmware's own code is compiled
# natively and checked against published SHA-256 / HMAC vectors.
HOSTCC ?= gcc
ifeq ($(OS),Windows_NT)
  HOSTEXE := .exe
else
  HOSTEXE :=
endif

test: | $(BUILD)
	"$(HOSTCC)" -std=c11 -Wall -Wextra -Iinclude -o $(BUILD)/valtest$(HOSTEXE) \
	  tools/valtest.c src/json.c src/chirpval.c src/sha256.c src/chirp_sign.c
	$(BUILD)/valtest$(HOSTEXE)

clean:
	rm -rf $(BUILD)

.PHONY: all clean test
