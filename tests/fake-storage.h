// hal/storage.h over host arrays, one per area. Include after the unit under
// test. Tests fill fakeArea[] directly and may cut writes short.
#ifndef TESTS_FAKE_STORAGE_H
#define TESTS_FAKE_STORAGE_H
#include "hal/storage.h"
#include <assert.h>
#include <string.h>

#define FAKE_AREA_BYTES 16384u
static uint8_t fakeArea[STORAGE_AREA_COUNT][FAKE_AREA_BYTES];
static uint32_t fakeSize[STORAGE_AREA_COUNT] = {
    [STORAGE_CONFIG_A] = 1024, [STORAGE_CONFIG_B] = 1024,
    [STORAGE_CHANNELS_A] = 12800, [STORAGE_CHANNELS_B] = 12800,
    [STORAGE_CALIBRATION] = 512, [STORAGE_RESOURCES] = 8856};
static bool fakeReady = true, fakeFailRead, fakeFailWrite;
static int fakeWriteLimit = -1; // Bytes kept of a cut write; -1 = all.
static unsigned fakeCutAt;      // Write number (fakeWrites) to cut; 0 = every one.
static unsigned fakeReadBytes, fakeWrites;
static StorageArea fakeLastWriteArea;
bool gStorageWrite;

bool STORAGE_Ready(void) { return fakeReady; }
uint32_t STORAGE_Size(StorageArea area) { return fakeReady ? fakeSize[area] : 0; }
uint32_t STORAGE_Capacity(void) { return 524288u; }
const char *STORAGE_Name(void) { return "fake"; }
void STORAGE_SetWritesEnabled(bool enabled) { (void)enabled; }
static bool fakeInRange(StorageArea area, uint32_t offset, uint16_t size) {
  return area < STORAGE_AREA_COUNT && size && offset < fakeSize[area] &&
         size <= fakeSize[area] - offset;
}
bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size) {
  if (fakeFailRead || !fakeInRange(area, offset, size)) return false;
  fakeReadBytes += size;
  memcpy(data, fakeArea[area] + offset, size);
  return true;
}
bool STORAGE_Write(StorageArea area, uint32_t offset, const void *data, uint16_t size) {
  assert(area <= STORAGE_CHANNELS_B && fakeInRange(area, offset, size));
  ++fakeWrites;
  fakeLastWriteArea = area;
  if (fakeFailWrite) return false;
  bool cut = fakeWriteLimit >= 0 && (!fakeCutAt || fakeWrites == fakeCutAt);
  unsigned n = cut ? (unsigned)fakeWriteLimit : size;
  if (n > size) n = size;
  memcpy(fakeArea[area] + offset, data, n);
  return n == size;
}
#endif
