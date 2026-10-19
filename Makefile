# =============================================================================
# Board
# =============================================================================
# uvk6: UV-K6 (DP32G030). Board code lives in src/boards/$(BOARD)/; its
# board.mk names the MCU, whose drivers live in src/drivers/mcu/$(MCU)/.
BOARD ?= uvk6
BOARD_DIR := src/boards/$(BOARD)
ifeq ($(wildcard $(BOARD_DIR)/board.mk),)
  $(error Unknown BOARD=$(BOARD): no $(BOARD_DIR)/board.mk)
endif
include $(BOARD_DIR)/board.mk
MCU_DIR := src/drivers/mcu/$(MCU)
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
SRC := $(SRC_DIR)/main.c \
       $(wildcard $(SRC_DIR)/core/*.c) \
       $(wildcard $(SRC_DIR)/hal/*.c) \
       $(wildcard $(BOARD_DIR)/*.c) \
       $(wildcard $(MCU_DIR)/*.c) \
       $(wildcard $(SRC_DIR)/drivers/*/*.c) \
       $(wildcard $(SRC_DIR)/ui/*.c) \
       $(wildcard $(SRC_DIR)/radio/*.c) \
       $(wildcard $(SRC_DIR)/ui/screens/*.c) \
       $(SRC_DIR)/system.c

OBJS := $(OBJ_DIR)/drivers/mcu/$(MCU)/start.o \
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

# BK4819 NFM transmit and PA control. ENABLE_TX=0 builds a receive-only image.
ENABLE_TX ?= 1
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
release: clean
	@$(MAKE) BUILD_TYPE=release all
	@cp $(TARGET).packed.bin $(BIN_DIR)/UVK6-Reborn-$(BUILD_TAG).bin
	@echo "Release firmware: $(BIN_DIR)/UVK6-Reborn-$(BUILD_TAG).bin"

# Генерация бинарного файла
$(TARGET).bin: $(TARGET) tools/fw-pack.py
	@echo "Creating binary file..."
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/fw-pack.py $@ $(GIT_HASH) $(TARGET).packed.bin

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
	arm-none-eabi-nm --size-sort -r $(BIN_DIR)/$(PROJECT_NAME) | head -20
	@echo ""

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
	$(PYTHON) tools/flash.py --port "$(PORT)" --firmware "$(TARGET).bin"

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
	@echo "  make BOARD=uvk6   # Board: uvk6 (default)"

# =============================================================================
# Dependencies
# =============================================================================
DEPS := $(OBJS:.o=.d)
-include $(DEPS)
