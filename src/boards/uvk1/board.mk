# UV-K1: factory-calibrated NFM RX/TX; external flash remains read-only.
MCU := py32f071
RADIO_CHIP := bk4829
override ENABLE_SI4732 := 0
PACK_FIRMWARE := 0
RELEASE_PREFIX := UVK1-Reborn
