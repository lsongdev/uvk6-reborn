# DP32G030: Cortex-M0; CMSIS uses the generic ARMCM0 device header.
CPU := cortex-m0
MCU_DEFINES := -DCMSIS_device_header=\"ARMCM0.h\" -DARMCM0
MCU_INC_DIRS := -I./src/external/CMSIS_5/Device/ARM/ARMCM0/Include
