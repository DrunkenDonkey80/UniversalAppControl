#include <Windows.h>
#include <stdio.h>
#include <TlHelp32.h>
#include <stdbool.h>

#include "keys.h"


static KEYCode keyCodes[] =
{
	{VK_A, L"VK_A"}, // Keyboard a and A
	{VK_B, L"VK_B"}, // Keyboard b and B
	{VK_C, L"VK_C"}, // Keyboard c and C
	{VK_D, L"VK_D"}, // Keyboard d and D
	{VK_E, L"VK_E"}, // Keyboard e and E
	{VK_F, L"VK_F"}, // Keyboard f and F
	{VK_G, L"VK_G"}, // Keyboard g and G
	{VK_H, L"VK_H"}, // Keyboard h and H
	{VK_I, L"VK_I"}, // Keyboard i and I
	{VK_J, L"VK_J"}, // Keyboard j and J
	{VK_K, L"VK_K"}, // Keyboard k and K
	{VK_L, L"VK_L"}, // Keyboard l and L
	{VK_M, L"VK_M"}, // Keyboard m and M
	{VK_N, L"VK_N"}, // Keyboard n and N
	{VK_O, L"VK_O"}, // Keyboard o and O
	{VK_P, L"VK_P"}, // Keyboard p and P
	{VK_Q, L"VK_Q"}, // Keyboard q and Q
	{VK_R, L"VK_R"}, // Keyboard r and R
	{VK_S, L"VK_S"}, // Keyboard s and S
	{VK_T, L"VK_T"}, // Keyboard t and T
	{VK_U, L"VK_U"}, // Keyboard u and U
	{VK_V, L"VK_V"}, // Keyboard v and V
	{VK_W, L"VK_W"}, // Keyboard w and W
	{VK_X, L"VK_X"}, // Keyboard x and X
	{VK_Y, L"VK_Y"}, // Keyboard y and Y
	{VK_Z, L"VK_Z"}, // Keyboard z and Z

	{VK_1, L"VK_1"}, // Keyboard 1 and !
	{VK_2, L"VK_2"}, // Keyboard 2 and @
	{VK_3, L"VK_3"}, // Keyboard 3 and #
	{VK_4, L"VK_4"}, // Keyboard 4 and $
	{VK_5, L"VK_5"}, // Keyboard 5 and %
	{VK_6, L"VK_6"}, // Keyboard 6 and ^
	{VK_7, L"VK_7"}, // Keyboard 7 and &
	{VK_8, L"VK_8"}, // Keyboard 8 and *
	{VK_9, L"VK_9"}, // Keyboard 9 and (
	{VK_0, L"VK_0"}, // Keyboard 0 and )

	{VK_RETURN, L"VK_RETURN"},     // Keyboard Return (ENTER)
	{VK_ESCAPE, L"VK_ESCAPE"},       // Keyboard ESCAPE
	{VK_BACK, L"VK_BACK"},     // Keyboard DELETE (Backspace)
	{VK_TAB, L"VK_TAB"},             // Keyboard Tab
	{VK_SPACE, L"VK_SPACE"},         // Keyboard Spacebar
	{VK_OEM_MINUS, L"VK_OEM_MINUS"}, // Keyboard - and _
	{VK_OEM_PLUS, L"VK_OEM_PLUS"},   // Keyboard = and +
	{VK_OEM_4, L"VK_OEM_4"},     // Keyboard [ and {
	{VK_OEM_6, L"VK_OEM_6"},    // Keyboard ] and }
	{VK_OEM_5, L"VK_OEM_5"},    // Keyboard \ and |
	{VK_OEM_3, L"VK_OEM_3"},     // Keyboard Non-US # and ~
	{VK_OEM_1, L"VK_OEM_1"},     // Keyboard ; and :
	{VK_OEM_7, L"VK_OEM_7"},  // Keyboard ' and "
	{VK_OEM_3, L"VK_OEM_3"},         // Keyboard ` and ~
	{VK_OEM_COMMA, L"VK_OEM_COMMA"}, // Keyboard , and <
	{VK_OEM_PERIOD, L"VK_OEM_PERIOD"}, // Keyboard . and >
	{VK_OEM_2, L"VK_OEM_2"},         // Keyboard / and ?
	{VK_CAPITAL, L"VK_CAPITAL"},  // Keyboard Caps Lock

	{VK_F1, L"VK_F1"},    // Keyboard F1
	{VK_F2, L"VK_F2"},    // Keyboard F2
	{VK_F3, L"VK_F3"},    // Keyboard F3
	{VK_F4, L"VK_F4"},    // Keyboard F4
	{VK_F5, L"VK_F5"},    // Keyboard F5
	{VK_F6, L"VK_F6"},    // Keyboard F6
	{VK_F7, L"VK_F7"},    // Keyboard F7
	{VK_F8, L"VK_F8"},    // Keyboard F8
	{VK_F9, L"VK_F9"},    // Keyboard F9
	{VK_F10, L"VK_F10"}, // Keyboard F10
	{VK_F11, L"VK_F11"}, // Keyboard F11
	{VK_F12, L"VK_F12"}, // Keyboard F12

	{VK_PRINT, L"VK_PRINT"},        // Keyboard Print Screen
	{VK_SCROLL, L"VK_SCROLL"}, // Keyboard Scroll Lock
	{VK_PAUSE, L"VK_PAUSE"},        // Keyboard Pause
	{VK_INSERT, L"VK_INSERT"},     // Keyboard Insert
	{VK_HOME, L"VK_HOME"},           // Keyboard Home
	{VK_PRIOR, L"VK_PRIOR"},       // Keyboard Page Up
	{VK_DELETE, L"VK_DELETE"},     // Keyboard Delete Forward
	{VK_END, L"VK_END"},              // Keyboard End
	{VK_NEXT, L"VK_NEXT"},       // Keyboard Page Down
	{VK_RIGHT, L"VK_RIGHT"},        // Keyboard Right Arrow
	{VK_LEFT, L"VK_LEFT"},           // Keyboard Left Arrow
	{VK_DOWN, L"VK_DOWN"},           // Keyboard Down Arrow
	{VK_UP, L"VK_UP"},                 // Keyboard Up Arrow

	{VK_NUMLOCK, L"VK_NUMLOCK"},    // Keyboard Num Lock and Clear
	{VK_DIVIDE, L"VK_DIVIDE"},        // Keypad /
	{VK_MULTIPLY, L"VK_MULTIPLY"}, // Keypad *
	{VK_SUBTRACT, L"VK_SUBTRACT"},    // Keypad -
	{VK_ADD, L"VK_ADD"},               // Keypad +
	{VK_RETURN, L"VK_RETURN"},      // Keypad ENTER
	{VK_NUMPAD1, L"VK_NUMPAD1"},          // Keypad 1 and End
	{VK_NUMPAD2, L"VK_NUMPAD2"},          // Keypad 2 and Down Arrow
	{VK_NUMPAD3, L"VK_NUMPAD3"},          // Keypad 3 and PageDn
	{VK_NUMPAD4, L"VK_NUMPAD4"},          // Keypad 4 and Left Arrow
	{VK_NUMPAD5, L"VK_NUMPAD5"},          // Keypad 5
	{VK_NUMPAD6, L"VK_NUMPAD6"},          // Keypad 6 and Right Arrow
	{VK_NUMPAD7, L"VK_NUMPAD7"},          // Keypad 7 and Home
	{VK_NUMPAD8, L"VK_NUMPAD8"},          // Keypad 8 and Up Arrow
	{VK_NUMPAD9, L"VK_NUMPAD9"},          // Keypad 9 and Page Up
	{VK_NUMPAD0, L"VK_NUMPAD0"},          // Keypad 0 and Insert
	{VK_DECIMAL, L"VK_DECIMAL"},        // Keypad . and Delete

	{VK_OEM_5, L"VK_OEM_5"}, // Keyboard Non-US \ and |

	{VK_F13, L"VK_F13"}, // Keyboard F13
	{VK_F14, L"VK_F14"}, // Keyboard F14
	{VK_F15, L"VK_F15"}, // Keyboard F15
	{VK_F16, L"VK_F16"}, // Keyboard F16
	{VK_F17, L"VK_F17"}, // Keyboard F17
	{VK_F18, L"VK_F18"}, // Keyboard F18
	{VK_F19, L"VK_F19"}, // Keyboard F19
	{VK_F20, L"VK_F20"}, // Keyboard F20
	{VK_F21, L"VK_F21"}, // Keyboard F21
	{VK_F22, L"VK_F22"}, // Keyboard F22
	{VK_F23, L"VK_F23"}, // Keyboard F23
	{VK_F24, L"VK_F24"}, // Keyboard F24

	{0x00, NULL}, // End
};

const KEYCode* findKeyWithName(const TCHAR* name) {
	for (int i = 0; keyCodes[i].vkCode != HID_KEY_NONE; i++) {
		if (!lstrcmpi(keyCodes[i].vkName, name) || !lstrcmpi(&keyCodes[i].vkName[3], name))
			return &keyCodes[i];
	}
	return NULL;
}
