# =============================================================================
# Board
# =============================================================================
# uvk6: UV-K6 (DP32G030). Board code lives in src/boards/$(BOARD)/; its
# board.mk names the MCU. Chip drivers sit side by side in src/drivers/;
# a directory with an mcu.mk is an MCU, and only the board's one is built.
BOARD ?= uvk6
BOARD_DIR := src/boards/$(BOARD)
ifeq ($(wildcard $(BOARD_DIR)/board.mk),)
  $(error Unknown BOARD=$(BOARD): no $(BOARD_DIR)/board.mk)
endif
PACK_FIRMWARE := 1
include $(BOARD_DIR)/board.mk
MCU_DIR := src/drivers/$(MCU)
include $(MCU_DIR)/mcu.mk

# =============================================================================
# Directory Structure
# =============================================================================
SRC_DIR       := src
OBJ_DIR       := obj/$(BOARD)
BIN_DIR       := bin/$(BOARD)

# =============================================================================
# Project Configuration
# =============================================================================
PROJECT_NAME  := firmware
TARGET        := $(BIN_DIR)/$(PROJECT_NAME)
GIT_HASH      := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
BUILD_TIME    := $(shell date -u +'%Y-%m-%d_%H:%M_UTC')
BUILD_TAG     := $(shell date -u +'%Y%m%d_%H%M')

# =============================================================================
# Source Files
# =============================================================================
OTHER_MCUS := $(filter-out $(MCU_DIR)/,$(dir $(wildcard $(SRC_DIR)/drivers/*/mcu.mk)))
DRIVER_SRC := $(filter-out $(addsuffix %,$(OTHER_MCUS)),$(wildcard $(SRC_DIR)/drivers/*/*.c))
BK48X9_BACKEND := $(SRC_DIR)/drivers/bk48x9/$(RADIO_CHIP).c
ifeq ($(wildcard $(BK48X9_BACKEND)),)
  $(error Unknown RADIO_CHIP=$(RADIO_CHIP))
endif
DRIVER_SRC := $(filter-out $(SRC_DIR)/drivers/bk48x9/bk4819.c $(SRC_DIR)/drivers/bk48x9/bk4829.c,$(DRIVER_SRC)) $(BK48X9_BACKEND)

SRC := $(wildcard $(SRC_DIR)/app/*.c) \
       $(wildcard $(SRC_DIR)/app/link/*.c) \
       $(wildcard $(SRC_DIR)/ui/*.c) \
       $(wildcard $(SRC_DIR)/ui/screens/*.c) \
       $(wildcard $(SRC_DIR)/radio/*.c) \
       $(wildcard $(SRC_DIR)/store/*.c) \
       $(wildcard $(SRC_DIR)/core/*.c) \
       $(wildcard $(SRC_DIR)/devices/*.c) \
       $(DRIVER_SRC) \
       $(wildcard $(BOARD_DIR)/*.c)

OBJS := $(OBJ_DIR)/drivers/$(MCU)/start.o \
        $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# =============================================================================
# BSP Configuration
# =============================================================================
BSP_DEFINITIONS := $(wildcard hardware/*/*.def)
BSP_HEADERS     := $(patsubst hardware/%,inc/%,$(BSP_DEFINITIONS:.def=.h))

# =============================================================================
# Toolchain
# =============================================================================
TOOLCHAIN_PREFIX := arm-none-eabi-
AS       := $(TOOLCHAIN_PREFIX)gcc
CC       := $(TOOLCHAIN_PREFIX)gcc
LD       := $(TOOLCHAIN_PREFIX)gcc
OBJCOPY  := $(TOOLCHAIN_PREFIX)objcopy
SIZE     := $(TOOLCHAIN_PREFIX)size
NM       := $(TOOLCHAIN_PREFIX)nm
PYTHON   ?= python3
PORT     ?=

# =============================================================================
# Compiler Flags
# =============================================================================
# Common flags for AS and CC; mcu.mk sets CPU.
COMMON_FLAGS := -mcpu=$(CPU) -mthumb -mabi=aapcs
OPTIMIZATION := -Os -flto=auto -ffunction-sections -fdata-sections
# OPTIMIZATION := -Os -ffunction-sections -fdata-sections

# Assembler flags
ASFLAGS  := $(COMMON_FLAGS) -c

# Compiler flags
CFLAGS   := $(COMMON_FLAGS) $(OPTIMIZATION) \
            -std=c2x \
            -Wall -Wextra -Wpedantic \
            -Wno-missing-field-initializers \
            -fno-builtin -fshort-enums \
            -fno-delete-null-pointer-checks \
            -fsingle-precision-constant \
            -finline-functions-called-once \
            -MMD -MP

# BK4819/BK4829 NFM TX and PA control. ENABLE_TX=0 builds an RX-only image.
ENABLE_TX ?= 1
# Exclusive auxiliary receiver selection: 1 = SI4732, 0 = BK1080.
# No runtime chip-type detection; each board.mk sets its fitted chip.
ENABLE_SI4732 ?= 0
# F→4 frequency capture (FC). ENABLE_FC=0 drops it and frees about 1.3 KiB.
ENABLE_FC ?= 1
# Slot menu → Spectrum: BK4819 sweep around the slot. ENABLE_SPECTRUM=0 drops it.
ENABLE_SPECTRUM ?= 1

# Debug/Release specific flags
DEBUG_FLAGS   := -g3 -DDEBUG -Og
RELEASE_FLAGS := -g0 -DNDEBUG

# Defines
DEFINES  := -DGIT_HASH=\"$(GIT_HASH)\" \
            -DTIME_STAMP=\"$(BUILD_TIME)\" \
            $(MCU_DEFINES) \
            -DUVK6_ENABLE_TX=$(ENABLE_TX) \
            -DUVK6_ENABLE_SI4732=$(ENABLE_SI4732) \
            -DUVK6_ENABLE_FC=$(ENABLE_FC) \
            -DUVK6_ENABLE_SPECTRUM=$(ENABLE_SPECTRUM)

# Include paths
INC_DIRS := -I./src \
            -I./src/external/CMSIS_5/CMSIS/Core/Include \
            $(MCU_INC_DIRS)

# =============================================================================
# Linker Flags
# =============================================================================
LDFLAGS  := $(COMMON_FLAGS) $(OPTIMIZATION) \
            -nostartfiles \
            -T$(BOARD_DIR)/firmware.ld \
            --specs=nano.specs \
            -lc -lnosys -lm \
            -Wl,--gc-sections \
            -Wl,--build-id=none \
            -Wl,--print-memory-usage \
            -Wl,-Map=$(OBJ_DIR)/output.map

# Library code the firmware must not pull in: signed 32-bit division (~460 B),
# 64-bit division, soft float, printf, heap. The link fails if any appears.
FORBIDDEN_HELPERS := ^(__aeabi_(idiv|idivmod|ldivmod|uldivmod|[fd][a-z0-9]+)|__(div|mod)[sd]i3|__u(div|mod)di3|.*printf|malloc)$$

# =============================================================================
# Build Configuration
# =============================================================================
# По умолчанию release сборка
BUILD_TYPE ?= release

ifeq ($(BUILD_TYPE),debug)
    CFLAGS += $(DEBUG_FLAGS)
    OPTIMIZATION := -Og
else
    CFLAGS += $(RELEASE_FLAGS)
endif

# =============================================================================
# Build Rules
# =============================================================================
.PHONY: all debug release clean help info flash

# Основная цель
all: $(TARGET).bin
	@echo "Build completed: $(TARGET).bin"

# Debug сборка
debug:
	@$(MAKE) BUILD_TYPE=debug all

# Release сборка с копированием
RELEASE_PREFIX ?= UVK6-Reborn

release: clean
	@$(MAKE) BUILD_TYPE=release all
	@cp $(RELEASE_IMAGE) $(BIN_DIR)/$(RELEASE_PREFIX)-$(BUILD_TAG).bin
	@echo "Release firmware: $(BIN_DIR)/$(RELEASE_PREFIX)-$(BUILD_TAG).bin"

ifeq ($(PACK_FIRMWARE),1)
RELEASE_IMAGE := $(TARGET).packed.bin
else
RELEASE_IMAGE := $(TARGET).bin
endif

# Генерация бинарного файла
$(TARGET).bin: $(TARGET) tools/fw-pack.py
	@echo "Creating binary file..."
	$(OBJCOPY) -O binary $< $@
ifeq ($(PACK_FIRMWARE),1)
	$(PYTHON) tools/fw-pack.py $@ $(GIT_HASH) $(TARGET).packed.bin
endif

# Линковка
$(TARGET): $(OBJS) | $(BIN_DIR)
	@echo "Linking..."
	$(LD) $(LDFLAGS) $^ -o $@
	@bad=$$($(NM) -j $@ | grep -E '$(FORBIDDEN_HELPERS)'); \
	if [ -n "$$bad" ]; then \
		echo "error: costly runtime helpers linked:" $$bad; \
		echo "  Signed / or % (often a uint8_t/uint16_t promoted to int): use unsigned operands, e.g. x % 60u."; \
		echo "  64-bit division, float or printf: avoid them; see docs/development.md §5."; \
		rm -f $@; exit 1; \
	fi
	@echo ""
	$(SIZE) $@
	arm-none-eabi-nm --size-sort -r $(TARGET) | head -20
	@echo ""

# Feature switches select code at compile time. Objects built with other
# switches are dropped: make 3.81 timestamps are too coarse to rely on.
BUILD_FLAGS := ENABLE_TX=$(ENABLE_TX) ENABLE_SI4732=$(ENABLE_SI4732) \
               ENABLE_FC=$(ENABLE_FC) ENABLE_SPECTRUM=$(ENABLE_SPECTRUM) \
               BUILD_TYPE=$(BUILD_TYPE)
FLAGS_FILE := $(OBJ_DIR)/build-flags
ifneq ($(BUILD_FLAGS),$(shell cat $(FLAGS_FILE) 2>/dev/null))
$(shell find $(OBJ_DIR) -name '*.o' -delete 2>/dev/null; mkdir -p $(OBJ_DIR) && echo '$(BUILD_FLAGS)' > $(FLAGS_FILE))
endif

# Компиляция C файлов
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(BSP_HEADERS) $(OBJ_DIR)
	@mkdir -p $(@D)
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(DEFINES) $(INC_DIRS) -c $< -o $@

# Компиляция ассемблерных файлов
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.S | $(OBJ_DIR)
	@mkdir -p $(@D)
	@echo "AS $<"
	@$(AS) $(ASFLAGS) $< -o $@

# Генерация BSP заголовков
inc/%/%.h: hardware/%/%.def
	@mkdir -p $(@D)
	@echo "Generating BSP header: $@"
	# TODO: Add your header generation command here
	@touch $@

# Создание директорий
$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

# =============================================================================
# Utility Targets
# =============================================================================

# Flash the raw image through the stock UV-K5/K6 serial bootloader.
flash: $(TARGET).bin
	@test -n "$(PORT)" || { echo "Usage: make flash PORT=/dev/cu.usbserial-1420 (Linux: /dev/ttyUSB0)"; exit 1; }
	$(PYTHON) tools/flash.py --board $(BOARD) --port "$(PORT)" --firmware "$(TARGET).bin"

# Показать информацию о сборке
info:
	@echo "Project Information:"
	@echo "  Project:     $(PROJECT_NAME)"
	@echo "  Git Hash:    $(GIT_HASH)"
	@echo "  Build Time:  $(BUILD_TIME)"
	@echo "  Board:       $(BOARD)"
	@echo "  Build Type:  $(BUILD_TYPE)"
	@echo ""
	@echo "Toolchain:"
	@echo "  CC:          $(CC)"
	@echo "  LD:          $(LD)"
	@echo ""
	@echo "Source Files: $(words $(SRC)) files"
	@echo "Object Files: $(words $(OBJS)) files"

# Очистка
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(OBJ_DIR) $(BIN_DIR) inc/
	@echo "Clean completed"

# Очистка всего включая зависимости
distclean: clean
	@echo "Deep cleaning..."
	@rm -rf $(BIN_DIR)
	@echo "Distclean completed"

# Помощь
help:
	@echo "Available targets:"
	@echo "  all      - Build firmware (default: release mode)"
	@echo "  debug    - Build firmware with debug symbols"
	@echo "  release  - Build release firmware with timestamp"
	@echo "  clean    - Remove build artifacts"
	@echo "  distclean- Remove all generated files"
	@echo "  info     - Show build configuration"
	@echo "  flash    - Build and flash raw firmware (requires PORT=serial-device)"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make              # Build release version"
	@echo "  make debug        # Build debug version"
	@echo "  make release      # Build and package release"
	@echo "  make BUILD_TYPE=debug  # Alternative debug build"
	@echo "  make flash PORT=/dev/cu.usbserial-1420  # Hold PTT while powering on"
	@echo "  make BOARD=uvk6   # Board: uvk6 (default) or uvk1"

# =============================================================================
# Dependencies
# =============================================================================
DEPS := $(OBJS:.o=.d)
-include $(DEPS)
