# =============================================================================
# Directory Structure
# =============================================================================
SRC_DIR       := src
OBJ_DIR       := obj
BIN_DIR       := bin

# =============================================================================
# Project Configuration
# =============================================================================
PROJECT_NAME  := firmware
TARGET        := $(BIN_DIR)/$(PROJECT_NAME)
GIT_HASH      := $(shell git rev-parse --short HEAD)
BUILD_TIME    := $(shell date -u +'"%Y-%m-%d %H:%M UTC"')
BUILD_TAG     := $(shell date -u +'"%Y%m%d_%H%M"')

# =============================================================================
# Source Files
# =============================================================================
SRC := $(wildcard $(SRC_DIR)/*.c) \
       $(wildcard $(SRC_DIR)/driver/*.c) \
       $(wildcard $(SRC_DIR)/helper/*.c) \
       $(wildcard $(SRC_DIR)/ui/*.c) \
       $(wildcard $(SRC_DIR)/apps/*.c)

OBJS := $(OBJ_DIR)/start.o \
        $(OBJ_DIR)/init.o \
        $(OBJ_DIR)/external/printf/printf.o \
        $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# =============================================================================
# BSP Configuration
# =============================================================================
BSP_DEFINITIONS := $(wildcard hardware/*/*.def)
BSP_HEADERS     := $(patsubst hardware/%,inc/%,$(BSP_DEFINITIONS:.def=.h))

# =============================================================================
# Toolchain
# =============================================================================
AS       := arm-none-eabi-gcc
CC       := arm-none-eabi-gcc
LD       := arm-none-eabi-gcc
OBJCOPY  := arm-none-eabi-objcopy
SIZE     := arm-none-eabi-size

# =============================================================================
# Compiler Flags
# =============================================================================
# Common flags for AS and CC
COMMON_FLAGS := -mcpu=cortex-m0 -mthumb -mabi=aapcs
OPTIMIZATION := -Os -flto=auto -ffunction-sections -fdata-sections

# Assembler flags
ASFLAGS  := $(COMMON_FLAGS) -c

# Compiler flags
CFLAGS   := $(COMMON_FLAGS) $(OPTIMIZATION) \
            -std=c2x \
            -Wall -Wextra \
			-Wno-missing-field-initializers \
            -Wno-error=incompatible-pointer-types \
            -Wno-unused-function -Wno-unused-variable \
            -fno-builtin -fshort-enums \
            -fno-delete-null-pointer-checks \
            -fsingle-precision-constant \
            -finline-functions-called-once \
            -MMD -MP

# Debug flags
DEBUG_FLAGS := -g3 -DDEBUG

# Defines
DEFINES  := -DPRINTF_INCLUDE_CONFIG_H \
            -DGIT_HASH=\"$(GIT_HASH)\" \
            -DTIME_STAMP=\"$(BUILD_TIME)\" \
            -DCMSIS_device_header=\"ARMCM0.h\" \
            -DARMCM0

# Include paths
INC_DIRS := -I./src/config \
            -I./src/external/CMSIS_5/CMSIS/Core/Include/ \
            -I./src/external/CMSIS_5/Device/ARM/ARMCM0/Include \
            -I./src/external/mcufont/decoder/ \
            -I./src/external/mcufont/fonts/

# =============================================================================
# Linker Flags
# =============================================================================
LDFLAGS  := $(COMMON_FLAGS) $(OPTIMIZATION) \
            -nostartfiles \
            -Tfirmware.ld \
            --specs=nano.specs \
            -lc -lnosys -lm \
            -Wl,--gc-sections \
            -Wl,--build-id=none \
            -Wl,--print-memory-usage \
            -Wl,-Map=$(OBJ_DIR)/output.map

# =============================================================================
# Build Rules
# =============================================================================
.PHONY: all debug release clean

all: $(TARGET).bin

debug: CFLAGS += $(DEBUG_FLAGS)
debug: all

release: clean all
	cp $(TARGET).packed.bin $(BIN_DIR)/s0va-by-fagci-$(BUILD_TAG).bin

$(TARGET).bin: $(TARGET)
	$(OBJCOPY) -O binary $< $@
	-python3 fw-pack.py $@ $(GIT_HASH) $(TARGET).packed.bin

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(LD) $(LDFLAGS) $^ -o $@
	$(SIZE) $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(BSP_HEADERS) $(OBJ_DIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEFINES) $(INC_DIRS) -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.S | $(OBJ_DIR)
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

inc/%/%.h: hardware/%/%.def
	@mkdir -p $(@D)
	# Add your header generation command here

$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

# =============================================================================
# Clean
# =============================================================================
clean:
	rm -rf $(TARGET) $(TARGET).* $(OBJ_DIR) $(BIN_DIR)/*.bin

# =============================================================================
# Dependencies
# =============================================================================
DEPS := $(OBJS:.o=.d)
-include $(DEPS)

