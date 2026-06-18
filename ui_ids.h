#pragma once

// Tray menu commands
#define IDM_SETTINGS   2001
#define IDM_STARTUP    2002
#define IDM_INSTALL    2003
#define IDM_OPENINI    2004
#define IDM_QUIT       2005

// Settings window controls
#define IDC_LIST       3001
#define IDC_NAME       3002
#define IDC_PATH       3003
#define IDC_HOTKEY     3004
#define IDC_HIDE       3005  // kept for legacy; use IDC_OPERATION
#define IDC_MIN        3006
#define IDC_PAUSE      3007
#define IDC_OPERATION  3025  // operation combobox (None/Hide/Min/Pause+Hide)

// Profile operation modes
#define PROF_OP_NONE    0
#define PROF_OP_HIDE    1
#define PROF_OP_MINIMIZE 2
#define PROF_OP_PAUSE   3   // hide windows then suspend; reverse on restore
#define IDC_ADD        3008
#define IDC_REMOVE     3009
#define IDC_STARTUP    3010
#define IDC_OPENINI    3011
#define IDC_INSTALL    3012
#define IDC_STATUS     3013

// Process picker controls
#define IDC_PICKLIST   3101
#define IDC_PICKOK     3102
#define IDC_PICKCANCEL 3103
#define IDC_PICKFILTER 3104

// Display control controls (settings window)
#define IDC_DISPLAYCTL 3200   // "Control monitor (DDC/CI)" master checkbox
#define IDC_PRESET     3201   // preset combo for selected profile
#define IDC_PRESETLBL  3202   // "Display preset:" label
#define IDC_PRESETEDIT 3203   // edit preset button

// Preset editor dialog
#define IDC_PE_NAME    3210   // preset name edit
#define IDC_PE_BRIGHT  3211   // brightness slider (trackbar)
#define IDC_PE_CONT    3212   // contrast slider (trackbar)
#define IDC_PE_CTEMP   3213   // color temp combo
#define IDC_PE_CAPTURE 3214   // "Capture current" button
#define IDC_PE_OK      3215
#define IDC_PE_CANCEL  3216
#define IDC_PE_BRIGHT_CHK  3217  // "set brightness" checkbox
#define IDC_PE_CONT_CHK    3218  // "set contrast" checkbox
#define IDC_PE_CTEMP_CHK   3219  // "set color temp" checkbox
#define IDC_PE_APPLY       3220  // "Apply to monitor" test button
#define IDC_PE_PROFILE_LBL 3221  // (unused - replaced by combo)
#define IDC_PE_PROFILE     3222  // combobox: monitor preset mode (VCP 0xF0)
#define IDC_PE_SCAN        3223  // "Scan" button: probe all F0 codes (unused)
#define IDC_PE_PROF_CHK    3224  // "Monitor preset" checkbox
