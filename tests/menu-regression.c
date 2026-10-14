// Exercise real menu input and number entry without display hardware.
#include "../src/ui/menu.c"
#include <assert.h>

void STATUSLINE_SetText(const char *text) { (void)text; }
void PrintSmall(uint8_t x, uint8_t y, const char *pattern) {
  (void)x; (void)y; (void)pattern;
}
void PrintMedium(uint8_t x, uint8_t y, const char *pattern) {
  (void)x; (void)y; (void)pattern;
}
void PrintSmallEx(uint8_t x, uint8_t y, TextPos pos, Color color, const char *pattern) {
  (void)x; (void)y; (void)pos; (void)color; (void)pattern;
}
void FillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color) {
  (void)x; (void)y; (void)w; (void)h; (void)color;
}
void DrawVLine(int16_t x, int16_t y, int16_t h, Color color) {
  (void)x; (void)y; (void)h; (void)color;
}
static unsigned accepts;
static uint32_t first;
static void accept(uint32_t value) { ++accepts; first=value; }
static void press(KEY_Code_t key) { assert(MENU_HandleInput(key, KEY_RELEASED)); }
int main(void) {
  MenuItem items[12] = {{.name="First"}};
  Menu menu = {.title="Test", .items=items, .num_items=8};
  MENU_Init(&menu);
  press(KEY_0); assert(menu.i==0 && !gIsNumNavInput);
  press(KEY_8); assert(menu.i==7 && !gIsNumNavInput);
  press(KEY_9); assert(menu.i==7 && !gIsNumNavInput);
  press(KEY_1); assert(menu.i==0 && !gIsNumNavInput);
  menu.num_items=12;
  press(KEY_1); assert(gIsNumNavInput);
  press(KEY_2); assert(menu.i==11 && !gIsNumNavInput);
  press(KEY_1); press(KEY_MENU); assert(menu.i==0 && !gIsNumNavInput);
  press(KEY_1); press(KEY_3); assert(menu.i==0 && !gIsNumNavInput);
  press(KEY_1); press(KEY_EXIT); assert(menu.i==0 && !gIsNumNavInput);
  press(KEY_1); MENU_Deinit(); assert(!gIsNumNavInput && !gNumNavCallback);
  menu.num_items=1; MENU_Init(&menu); MENU_Render();
  MENU_EditNumber(500,860,100,824,"V",accept);
  press(KEY_7);press(KEY_STAR);press(KEY_8);press(KEY_6);MENU_Render();
  press(KEY_PTT);assert(number.active && !accepts && menu.i==0);
  press(KEY_MENU);assert(accepts==1 && first==786 && !number.active);
  MENU_EditNumber(500,860,100,824,"V",accept);
  press(KEY_4);press(KEY_MENU);assert(number.active && number.invalid && accepts==1);
  MENU_Render();assert(MENU_HandleInput(KEY_EXIT,KEY_LONG_PRESSED));
  assert(!number.active && accepts==1 && MENU_IsActive());
  // EXIT deletes the draft; a long EXIT abandons the edit.
  MENU_EditNumber(0,1340000000,1000000,438500000,"MHz",accept);
  press(KEY_EXIT);assert(number.active && !strcmp(number.text,"438.50000"));
  assert(MENU_HandleInput(KEY_EXIT,KEY_LONG_PRESSED));assert(!number.active && accepts==1);
  MENU_EditNumber(500,860,100,824,"V",accept);
  MENU_Deinit();assert(!number.active);
  MENU_Init(&menu);
  menu.num_items=0; MENU_Render(); assert(!MENU_HandleInput(KEY_MENU,KEY_RELEASED));
  return 0;
}
