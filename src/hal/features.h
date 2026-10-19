#ifndef HAL_FEATURES_H
#define HAL_FEATURES_H
// Deterministic, exclusive auxiliary receiver selection. K1 board.mk fixes 0.
#ifndef UVK6_ENABLE_SI4732
#define UVK6_ENABLE_SI4732 0
#endif
#if UVK6_ENABLE_SI4732 != 0 && UVK6_ENABLE_SI4732 != 1
#error UVK6_ENABLE_SI4732 must be 0 or 1
#endif
#endif
