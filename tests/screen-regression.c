// Screen stack: enter/leave order, the root stays, keys go to the top only.
#include "../src/ui/screens/screen.c"
#include <assert.h>
#include <string.h>

bool gRedrawScreen;
static char log[64], title[16];
void STATUSLINE_SetText(const char *text) { strncpy(title, text, sizeof(title) - 1); }
static void mark(char c) { size_t n = strlen(log); log[n] = c; log[n + 1] = 0; }
static void enterA(void) { mark('A'); }
static void leaveA(void) { mark('a'); }
static void enterB(void) { mark('B'); }
static void leaveB(void) { mark('b'); }
static unsigned keysA, keysB, updatesB, rendersB;
static bool keyA(KEY_Code_t key, Key_State_t state) { (void)key; (void)state; ++keysA; return true; }
static bool keyB(KEY_Code_t key, Key_State_t state) { (void)state; ++keysB; return key != KEY_EXIT; }
static void updateB(void) { ++updatesB; }
static void renderB(void) { ++rendersB; }
static const Screen a = { .title = "A", .enter = enterA, .leave = leaveA, .key = keyA };
static const Screen b = { .title = "B", .enter = enterB, .leave = leaveB, .key = keyB,
                          .update = updateB, .render = renderB };
static const Screen bare = {0}; // Every hook is optional.

int main(void) {
  SCREEN_Reset(&a);
  assert(!strcmp(log, "A") && SCREEN_Top() == &a && !strcmp(title, "A") && gRedrawScreen);
  assert(!SCREEN_Pop() && SCREEN_Top() == &a); // The root stays.
  SCREEN_Push(&b);
  assert(!strcmp(log, "AaB") && SCREEN_Top() == &b && !strcmp(title, "B"));
  SCREEN_Push(&b);
  assert(!strcmp(log, "AaB")); // Pushing the top again is ignored.
  // Keys, updates and drawing reach only the top screen.
  assert(SCREEN_Key(KEY_1, KEY_RELEASED) && keysB == 1 && !keysA);
  assert(!SCREEN_Key(KEY_EXIT, KEY_RELEASED)); // Unhandled: the caller decides.
  SCREEN_Update(); SCREEN_Render();
  assert(updatesB == 1 && rendersB == 1);
  assert(SCREEN_Pop() && !strcmp(log, "AaBbA") && SCREEN_Top() == &a && !strcmp(title, "A"));
  // A screen without hooks is fine anywhere.
  SCREEN_Push(&bare);
  assert(!SCREEN_Key(KEY_1, KEY_RELEASED));
  SCREEN_Update(); SCREEN_Render();
  assert(SCREEN_Pop());
  // The stack is bounded; a push beyond it is refused, the root survives.
  log[0] = 0;
  for (unsigned i = 0; i < 10; ++i) SCREEN_Push(i % 2 ? &a : &b);
  unsigned depthPushed = 0;
  while (SCREEN_Pop()) ++depthPushed;
  assert(depthPushed == DEPTH - 1 && SCREEN_Top() == &a);
  // Reset leaves every screen, top first, and shows the new root.
  SCREEN_Push(&b); log[0] = 0;
  SCREEN_Reset(&b);
  assert(!strcmp(log, "baB") && SCREEN_Top() == &b);
  return 0;
}
