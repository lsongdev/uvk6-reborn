# Quansheng UV-K6 (512 KiB EEPROM edition).
MCU := dp32g030
RADIO_CHIP := bk4819
# This K6 carries the SI4732 mod; ENABLE_SI4732=0 builds for a stock BK1080.
ENABLE_SI4732 ?= 1
