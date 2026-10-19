#include "ui/screens/screen.h"
#include "ui/screens/statusline.h"
#include "devices/display.h"
#include <stddef.h>

#define DEPTH 6
static const Screen *stack[DEPTH];
static unsigned depth;

static void entered(void) {
  const Screen *top = stack[depth - 1];
  if (top->title) STATUSLINE_SetText(top->title);
  if (top->enter) top->enter();
  gRedrawScreen = true;
}
static void left(void) {
  const Screen *top = stack[depth - 1];
  if (top->leave) top->leave();
}

void SCREEN_Reset(const Screen *root) {
  while (depth) { left(); --depth; }
  stack[depth++] = root;
  entered();
}
// Screens are shallow (home → menu → input); a full stack is a bug, so the
// push is refused rather than silently dropping the root.
void SCREEN_Push(const Screen *screen) {
  if (!screen || depth >= DEPTH || (depth && stack[depth - 1] == screen)) return;
  if (depth) left();
  stack[depth++] = screen;
  entered();
}
bool SCREEN_Pop(void) {
  if (depth <= 1) return false;
  left();
  --depth;
  entered();
  return true;
}
const Screen *SCREEN_Top(void) { return depth ? stack[depth - 1] : NULL; }
bool SCREEN_Key(KEY_Code_t key, Key_State_t state) {
  const Screen *top = SCREEN_Top();
  return top && top->key && top->key(key, state);
}
void SCREEN_Update(void) {
  const Screen *top = SCREEN_Top();
  if (top && top->update) top->update();
}
void SCREEN_Render(void) {
  const Screen *top = SCREEN_Top();
  if (top && top->render) top->render();
}
