/**
 * USB HID Keyboard scan codes as per USB spec 1.11
 * plus some additional codes
 *
 * Created by MightyPork, 2016
 * Public domain
 *
 * Adapted from:
 * https://source.android.com/devices/input/keyboard-devices.html
 */

#ifndef HID_KEYS
#define HID_KEYS

/**
 * Modifier masks - used for the first byte in the HID report.
 * NOTE: The second byte in the report is reserved, 0x00
 */
#define HID_KEY_MOD_LCTRL 0x01
#define HID_KEY_MOD_LSHIFT 0x02
#define HID_KEY_MOD_LALT 0x04
#define HID_KEY_MOD_LMETA 0x08
#define HID_KEY_MOD_RCTRL 0x10
#define HID_KEY_MOD_RSHIFT 0x20
#define HID_KEY_MOD_RALT 0x40
#define HID_KEY_MOD_RMETA 0x80

/**
 * Scan codes - last N slots in the HID report (usually 6).
 * 0x00 if no key pressed.
 *
 * If more than N keys are pressed, the HID reports
 * HID_KEY_ERR_OVF in all slots to indicate this condition.
 */

#define HID_KEY_NONE 0x00    // No key pressed
#define HID_KEY_ERR_OVF 0x01 //  Keyboard Error Roll Over - used for all slots if too many keys are pressed ("Phantom key")
// 0x02 //  Keyboard POST Fail
// 0x03 //  Keyboard Error Undefined
#define HID_KEY_A 0x04 // Keyboard a and A
#define HID_KEY_B 0x05 // Keyboard b and B
#define HID_KEY_C 0x06 // Keyboard c and C
#define HID_KEY_D 0x07 // Keyboard d and D
#define HID_KEY_E 0x08 // Keyboard e and E
#define HID_KEY_F 0x09 // Keyboard f and F
#define HID_KEY_G 0x0a // Keyboard g and G
#define HID_KEY_H 0x0b // Keyboard h and H
#define HID_KEY_I 0x0c // Keyboard i and I
#define HID_KEY_J 0x0d // Keyboard j and J
#define HID_KEY_K 0x0e // Keyboard k and K
#define HID_KEY_L 0x0f // Keyboard l and L
#define HID_KEY_M 0x10 // Keyboard m and M
#define HID_KEY_N 0x11 // Keyboard n and N
#define HID_KEY_O 0x12 // Keyboard o and O
#define HID_KEY_P 0x13 // Keyboard p and P
#define HID_KEY_Q 0x14 // Keyboard q and Q
#define HID_KEY_R 0x15 // Keyboard r and R
#define HID_KEY_S 0x16 // Keyboard s and S
#define HID_KEY_T 0x17 // Keyboard t and T
#define HID_KEY_U 0x18 // Keyboard u and U
#define HID_KEY_V 0x19 // Keyboard v and V
#define HID_KEY_W 0x1a // Keyboard w and W
#define HID_KEY_X 0x1b // Keyboard x and X
#define HID_KEY_Y 0x1c // Keyboard y and Y
#define HID_KEY_Z 0x1d // Keyboard z and Z

#define HID_KEY_1 0x1e // Keyboard 1 and !
#define HID_KEY_2 0x1f // Keyboard 2 and @
#define HID_KEY_3 0x20 // Keyboard 3 and #
#define HID_KEY_4 0x21 // Keyboard 4 and $
#define HID_KEY_5 0x22 // Keyboard 5 and %
#define HID_KEY_6 0x23 // Keyboard 6 and ^
#define HID_KEY_7 0x24 // Keyboard 7 and &
#define HID_KEY_8 0x25 // Keyboard 8 and *
#define HID_KEY_9 0x26 // Keyboard 9 and (
#define HID_KEY_0 0x27 // Keyboard 0 and )

#define HID_KEY_ENTER 0x28      // Keyboard Return (ENTER)
#define HID_KEY_ESC 0x29        // Keyboard ESCAPE
#define HID_KEY_BACKSPACE 0x2a  // Keyboard DELETE (Backspace)
#define HID_KEY_TAB 0x2b        // Keyboard Tab
#define HID_KEY_SPACE 0x2c      // Keyboard Spacebar
#define HID_KEY_MINUS 0x2d      // Keyboard - and _
#define HID_KEY_EQUAL 0x2e      // Keyboard = and +
#define HID_KEY_LEFTBRACE 0x2f  // Keyboard [ and {
#define HID_KEY_RIGHTBRACE 0x30 // Keyboard ] and }
#define HID_KEY_BACKSLASH 0x31  // Keyboard \ and |
#define HID_KEY_HASHTILDE 0x32  // Keyboard Non-US # and ~
#define HID_KEY_SEMICOLON 0x33  // Keyboard ; and :
#define HID_KEY_APOSTROPHE 0x34 // Keyboard ' and "
#define HID_KEY_GRAVE 0x35      // Keyboard ` and ~
#define HID_KEY_COMMA 0x36      // Keyboard , and <
#define HID_KEY_DOT 0x37        // Keyboard . and >
#define HID_KEY_SLASH 0x38      // Keyboard / and ?
#define HID_KEY_CAPSLOCK 0x39   // Keyboard Caps Lock

#define HID_KEY_F1 0x3a  // Keyboard F1
#define HID_KEY_F2 0x3b  // Keyboard F2
#define HID_KEY_F3 0x3c  // Keyboard F3
#define HID_KEY_F4 0x3d  // Keyboard F4
#define HID_KEY_F5 0x3e  // Keyboard F5
#define HID_KEY_F6 0x3f  // Keyboard F6
#define HID_KEY_F7 0x40  // Keyboard F7
#define HID_KEY_F8 0x41  // Keyboard F8
#define HID_KEY_F9 0x42  // Keyboard F9
#define HID_KEY_F10 0x43 // Keyboard F10
#define HID_KEY_F11 0x44 // Keyboard F11
#define HID_KEY_F12 0x45 // Keyboard F12

#define HID_KEY_SYSRQ 0x46      // Keyboard Print Screen
#define HID_KEY_SCROLLLOCK 0x47 // Keyboard Scroll Lock
#define HID_KEY_PAUSE 0x48      // Keyboard Pause
#define HID_KEY_INSERT 0x49     // Keyboard Insert
#define HID_KEY_HOME 0x4a       // Keyboard Home
#define HID_KEY_PAGEUP 0x4b     // Keyboard Page Up
#define HID_KEY_DELETE 0x4c     // Keyboard Delete Forward
#define HID_KEY_END 0x4d        // Keyboard End
#define HID_KEY_PAGEDOWN 0x4e   // Keyboard Page Down
#define HID_KEY_RIGHT 0x4f      // Keyboard Right Arrow
#define HID_KEY_LEFT 0x50       // Keyboard Left Arrow
#define HID_KEY_DOWN 0x51       // Keyboard Down Arrow
#define HID_KEY_UP 0x52         // Keyboard Up Arrow

#define HID_KEY_NUMLOCK 0x53    // Keyboard Num Lock and Clear
#define HID_KEY_KPSLASH 0x54    // Keypad /
#define HID_KEY_KPASTERISK 0x55 // Keypad *
#define HID_KEY_KPMINUS 0x56    // Keypad -
#define HID_KEY_KPPLUS 0x57     // Keypad +
#define HID_KEY_KPENTER 0x58    // Keypad ENTER
#define HID_KEY_KP1 0x59        // Keypad 1 and End
#define HID_KEY_KP2 0x5a        // Keypad 2 and Down Arrow
#define HID_KEY_KP3 0x5b        // Keypad 3 and PageDn
#define HID_KEY_KP4 0x5c        // Keypad 4 and Left Arrow
#define HID_KEY_KP5 0x5d        // Keypad 5
#define HID_KEY_KP6 0x5e        // Keypad 6 and Right Arrow
#define HID_KEY_KP7 0x5f        // Keypad 7 and Home
#define HID_KEY_KP8 0x60        // Keypad 8 and Up Arrow
#define HID_KEY_KP9 0x61        // Keypad 9 and Page Up
#define HID_KEY_KP0 0x62        // Keypad 0 and Insert
#define HID_KEY_KPDOT 0x63      // Keypad . and Delete

#define HID_KEY_102ND 0x64   // Keyboard Non-US \ and |
#define HID_KEY_COMPOSE 0x65 // Keyboard Application
#define HID_KEY_POWER 0x66   // Keyboard Power
#define HID_KEY_KPEQUAL 0x67 // Keypad =

#define HID_KEY_F13 0x68 // Keyboard F13
#define HID_KEY_F14 0x69 // Keyboard F14
#define HID_KEY_F15 0x6a // Keyboard F15
#define HID_KEY_F16 0x6b // Keyboard F16
#define HID_KEY_F17 0x6c // Keyboard F17
#define HID_KEY_F18 0x6d // Keyboard F18
#define HID_KEY_F19 0x6e // Keyboard F19
#define HID_KEY_F20 0x6f // Keyboard F20
#define HID_KEY_F21 0x70 // Keyboard F21
#define HID_KEY_F22 0x71 // Keyboard F22
#define HID_KEY_F23 0x72 // Keyboard F23
#define HID_KEY_F24 0x73 // Keyboard F24

#define HID_KEY_OPEN 0x74       // Keyboard Execute
#define HID_KEY_HELP 0x75       // Keyboard Help
#define HID_KEY_PROPS 0x76      // Keyboard Menu
#define HID_KEY_FRONT 0x77      // Keyboard Select
#define HID_KEY_STOP 0x78       // Keyboard Stop
#define HID_KEY_AGAIN 0x79      // Keyboard Again
#define HID_KEY_UNDO 0x7a       // Keyboard Undo
#define HID_KEY_CUT 0x7b        // Keyboard Cut
#define HID_KEY_COPY 0x7c       // Keyboard Copy
#define HID_KEY_PASTE 0x7d      // Keyboard Paste
#define HID_KEY_FIND 0x7e       // Keyboard Find
#define HID_KEY_MUTE 0x7f       // Keyboard Mute
#define HID_KEY_VOLUMEUP 0x80   // Keyboard Volume Up
#define HID_KEY_VOLUMEDOWN 0x81 // Keyboard Volume Down
// 0x82  Keyboard Locking Caps Lock
// 0x83  Keyboard Locking Num Lock
// 0x84  Keyboard Locking Scroll Lock
#define HID_KEY_KPCOMMA 0x85 // Keypad Comma
// 0x86  Keypad Equal Sign
#define HID_KEY_RO 0x87               // Keyboard International1
#define HID_KEY_KATAKANAHIRAGANA 0x88 // Keyboard International2
#define HID_KEY_YEN 0x89              // Keyboard International3
#define HID_KEY_HENKAN 0x8a           // Keyboard International4
#define HID_KEY_MUHENKAN 0x8b         // Keyboard International5
#define HID_KEY_KPJPCOMMA 0x8c        // Keyboard International6
// 0x8d  Keyboard International7
// 0x8e  Keyboard International8
// 0x8f  Keyboard International9
#define HID_KEY_HANGEUL 0x90        // Keyboard LANG1
#define HID_KEY_HANJA 0x91          // Keyboard LANG2
#define HID_KEY_KATAKANA 0x92       // Keyboard LANG3
#define HID_KEY_HIRAGANA 0x93       // Keyboard LANG4
#define HID_KEY_ZENKAKUHANKAKU 0x94 // Keyboard LANG5
// 0x95  Keyboard LANG6
// 0x96  Keyboard LANG7
// 0x97  Keyboard LANG8
// 0x98  Keyboard LANG9
// 0x99  Keyboard Alternate Erase
// 0x9a  Keyboard SysReq/Attention
// 0x9b  Keyboard Cancel
// 0x9c  Keyboard Clear
// 0x9d  Keyboard Prior
// 0x9e  Keyboard Return
// 0x9f  Keyboard Separator
// 0xa0  Keyboard Out
// 0xa1  Keyboard Oper
// 0xa2  Keyboard Clear/Again
// 0xa3  Keyboard CrSel/Props
// 0xa4  Keyboard ExSel

// 0xb0  Keypad 00
// 0xb1  Keypad 000
// 0xb2  Thousands Separator
// 0xb3  Decimal Separator
// 0xb4  Currency Unit
// 0xb5  Currency Sub-unit
#define HID_KEY_KPLEFTPAREN 0xb6  // Keypad (
#define HID_KEY_KPRIGHTPAREN 0xb7 // Keypad )
// 0xb8  Keypad {
// 0xb9  Keypad }
// 0xba  Keypad Tab
// 0xbb  Keypad Backspace
// 0xbc  Keypad A
// 0xbd  Keypad B
// 0xbe  Keypad C
// 0xbf  Keypad D
// 0xc0  Keypad E
// 0xc1  Keypad F
// 0xc2  Keypad XOR
// 0xc3  Keypad ^
// 0xc4  Keypad %
// 0xc5  Keypad <
// 0xc6  Keypad >
// 0xc7  Keypad &
// 0xc8  Keypad &&
// 0xc9  Keypad |
// 0xca  Keypad ||
// 0xcb  Keypad :
// 0xcc  Keypad #
// 0xcd  Keypad Space
// 0xce  Keypad @
// 0xcf  Keypad !
// 0xd0  Keypad Memory Store
// 0xd1  Keypad Memory Recall
// 0xd2  Keypad Memory Clear
// 0xd3  Keypad Memory Add
// 0xd4  Keypad Memory Subtract
// 0xd5  Keypad Memory Multiply
// 0xd6  Keypad Memory Divide
// 0xd7  Keypad +/-
// 0xd8  Keypad Clear
// 0xd9  Keypad Clear Entry
// 0xda  Keypad Binary
// 0xdb  Keypad Octal
// 0xdc  Keypad Decimal
// 0xdd  Keypad Hexadecimal

#define HID_KEY_LEFTCTRL 0xe0   // Keyboard Left Control
#define HID_KEY_LEFTSHIFT 0xe1  // Keyboard Left Shift
#define HID_KEY_LEFTALT 0xe2    // Keyboard Left Alt
#define HID_KEY_LEFTMETA 0xe3   // Keyboard Left GUI
#define HID_KEY_RIGHTCTRL 0xe4  // Keyboard Right Control
#define HID_KEY_RIGHTSHIFT 0xe5 // Keyboard Right Shift
#define HID_KEY_RIGHTALT 0xe6   // Keyboard Right Alt
#define HID_KEY_RIGHTMETA 0xe7  // Keyboard Right GUI

#define HID_KEY_MEDIA_PLAYPAUSE 0xe8
#define HID_KEY_MEDIA_STOPCD 0xe9
#define HID_KEY_MEDIA_PREVIOUSSONG 0xea
#define HID_KEY_MEDIA_NEXTSONG 0xeb
#define HID_KEY_MEDIA_EJECTCD 0xec
#define HID_KEY_MEDIA_VOLUMEUP 0xed
#define HID_KEY_MEDIA_VOLUMEDOWN 0xee
#define HID_KEY_MEDIA_MUTE 0xef
#define HID_KEY_MEDIA_WWW 0xf0
#define HID_KEY_MEDIA_BACK 0xf1
#define HID_KEY_MEDIA_FORWARD 0xf2
#define HID_KEY_MEDIA_STOP 0xf3
#define HID_KEY_MEDIA_FIND 0xf4
#define HID_KEY_MEDIA_SCROLLUP 0xf5
#define HID_KEY_MEDIA_SCROLLDOWN 0xf6
#define HID_KEY_MEDIA_EDIT 0xf7
#define HID_KEY_MEDIA_SLEEP 0xf8
#define HID_KEY_MEDIA_COFFEE 0xf9
#define HID_KEY_MEDIA_REFRESH 0xfa
#define HID_KEY_MEDIA_CALC 0xfb

/*
 * Virtual Keys, Standard Set
 */
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04 /* NOT contiguous with L & RBUTTON */

#define VK_XBUTTON1 0x05 /* NOT contiguous with L & RBUTTON */
#define VK_XBUTTON2 0x06 /* NOT contiguous with L & RBUTTON */

/*
 * 0x07 : reserved
 */

#define VK_BACK 0x08
#define VK_TAB 0x09

/*
 * 0x0A - 0x0B : reserved
 */

#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D

/*
 * 0x0E - 0x0F : unassigned
 */

#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14

#define VK_KANA 0x15
#define VK_HANGEUL 0x15 /* old name - should be here for compatibility */
#define VK_HANGUL 0x15

/*
 * 0x16 : unassigned
 */

#define VK_JUNJA 0x17
#define VK_FINAL 0x18
#define VK_HANJA 0x19
#define VK_KANJI 0x19

/*
 * 0x1A : unassigned
 */

#define VK_ESCAPE 0x1B

#define VK_CONVERT 0x1C
#define VK_NONCONVERT 0x1D
#define VK_ACCEPT 0x1E
#define VK_MODECHANGE 0x1F

#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_EXECUTE 0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F

/*
 * VK_0 - VK_9 are the same as ASCII '0' - '9' (0x30 - 0x39)
 * 0x3A - 0x40 : unassigned
 * VK_A - VK_Z are the same as ASCII 'A' - 'Z' (0x41 - 0x5A)
 */
#define VK_A (unsigned char)'A'
#define VK_B (unsigned char)'B'
#define VK_C (unsigned char)'C'
#define VK_D (unsigned char)'D'
#define VK_E (unsigned char)'E'
#define VK_F (unsigned char)'F'
#define VK_G (unsigned char)'G'
#define VK_H (unsigned char)'H'
#define VK_I (unsigned char)'I'
#define VK_J (unsigned char)'J'
#define VK_K (unsigned char)'K'
#define VK_L (unsigned char)'L'
#define VK_M (unsigned char)'M'
#define VK_N (unsigned char)'N'
#define VK_O (unsigned char)'O'
#define VK_P (unsigned char)'P'
#define VK_Q (unsigned char)'Q'
#define VK_R (unsigned char)'R'
#define VK_S (unsigned char)'S'
#define VK_T (unsigned char)'T'
#define VK_U (unsigned char)'U'
#define VK_V (unsigned char)'V'
#define VK_W (unsigned char)'W'
#define VK_X (unsigned char)'X'
#define VK_Y (unsigned char)'Y'
#define VK_Z (unsigned char)'Z'

#define VK_1 (unsigned char)'1'
#define VK_2 (unsigned char)'2'
#define VK_3 (unsigned char)'3'
#define VK_4 (unsigned char)'4'
#define VK_5 (unsigned char)'5'
#define VK_6 (unsigned char)'6'
#define VK_7 (unsigned char)'7'
#define VK_8 (unsigned char)'8'
#define VK_9 (unsigned char)'9'
#define VK_0 (unsigned char)'0'

#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D

/*
 * 0x5E : reserved
 */

#define VK_SLEEP 0x5F

#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_F20 0x83
#define VK_F21 0x84
#define VK_F22 0x85
#define VK_F23 0x86
#define VK_F24 0x87

/*
 * 0x88 - 0x8F : UI navigation
 */

#define VK_NAVIGATION_VIEW 0x88   // reserved
#define VK_NAVIGATION_MENU 0x89   // reserved
#define VK_NAVIGATION_UP 0x8A     // reserved
#define VK_NAVIGATION_DOWN 0x8B   // reserved
#define VK_NAVIGATION_LEFT 0x8C   // reserved
#define VK_NAVIGATION_RIGHT 0x8D  // reserved
#define VK_NAVIGATION_ACCEPT 0x8E // reserved
#define VK_NAVIGATION_CANCEL 0x8F // reserved

#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91

/*
 * NEC PC-9800 kbd definitions
 */
#define VK_OEM_NEC_EQUAL 0x92 // '=' key on numpad

/*
 * Fujitsu/OASYS kbd definitions
 */
#define VK_OEM_FJ_JISHO 0x92   // 'Dictionary' key
#define VK_OEM_FJ_MASSHOU 0x93 // 'Unregister word' key
#define VK_OEM_FJ_TOUROKU 0x94 // 'Register word' key
#define VK_OEM_FJ_LOYA 0x95    // 'Left OYAYUBI' key
#define VK_OEM_FJ_ROYA 0x96    // 'Right OYAYUBI' key

/*
 * 0x97 - 0x9F : unassigned
 */

/*
 * VK_L* & VK_R* - left and right Alt, Ctrl and Shift virtual keys.
 * Used only as parameters to GetAsyncKeyState() and GetKeyState().
 * No other API or message will distinguish left and right keys in this way.
 */
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5

#define VK_BROWSER_BACK 0xA6
#define VK_BROWSER_FORWARD 0xA7
#define VK_BROWSER_REFRESH 0xA8
#define VK_BROWSER_STOP 0xA9
#define VK_BROWSER_SEARCH 0xAA
#define VK_BROWSER_FAVORITES 0xAB
#define VK_BROWSER_HOME 0xAC

#define VK_VOLUME_MUTE 0xAD
#define VK_VOLUME_DOWN 0xAE
#define VK_VOLUME_UP 0xAF
#define VK_MEDIA_NEXT_TRACK 0xB0
#define VK_MEDIA_PREV_TRACK 0xB1
#define VK_MEDIA_STOP 0xB2
#define VK_MEDIA_PLAY_PAUSE 0xB3
#define VK_LAUNCH_MAIL 0xB4
#define VK_LAUNCH_MEDIA_SELECT 0xB5
#define VK_LAUNCH_APP1 0xB6
#define VK_LAUNCH_APP2 0xB7

/*
 * 0xB8 - 0xB9 : reserved
 */

#define VK_OEM_1 0xBA      // ';:' for US
#define VK_OEM_PLUS 0xBB   // '+' any country
#define VK_OEM_COMMA 0xBC  // ',' any country
#define VK_OEM_MINUS 0xBD  // '-' any country
#define VK_OEM_PERIOD 0xBE // '.' any country
#define VK_OEM_2 0xBF      // '/?' for US
#define VK_OEM_3 0xC0      // '`~' for US

#define VK_OEM_4 0xDB //  '[{' for US
#define VK_OEM_5 0xDC //  '\|' for US
#define VK_OEM_6 0xDD //  ']}' for US
#define VK_OEM_7 0xDE //  ''"' for US
#define VK_OEM_8 0xDF

#endif // HID_KEYS

typedef struct KEYCode {
	unsigned char vkCode;
	const TCHAR* vkName;
} KEYCode;


extern const KEYCode* findKeyWithName(const TCHAR* name);
extern const wchar_t* FindKeyNameByVk(unsigned long vk);