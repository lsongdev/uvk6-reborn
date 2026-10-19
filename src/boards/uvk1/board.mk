# UV-K1 bring-up: receive-only, no external flash programming/erasing.
MCU := py32f071
override ENABLE_TX := 0
PACK_FIRMWARE := 0
RELEASE_PREFIX := UVK1-Reborn
