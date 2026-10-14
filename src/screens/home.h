#ifndef SCREENS_HOME_H
#define SCREENS_HOME_H
#include "screens/screen.h"

// The main screen: slot cards, keypad frequency entry, the F prefix and the
// tune confirmation. The radio itself runs whether or not this is shown.
extern const Screen homeScreen;
void HOME_CancelShortcut(void); // E.g. when the keyboard lock toggles.
#endif
