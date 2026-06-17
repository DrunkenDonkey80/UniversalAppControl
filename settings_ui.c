#include "settings_ui.h"
#include "ui_ids.h"
#include "Main.h"
#include "resource.h"
#include "config.h"
#include "install.h"
#include "worker.h"
#include "display.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <tlhelp32.h>
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")
#include <uxtheme.h>
#pragma comment(lib, "UxTheme.lib")
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")

// ---- Theme palette ----
#define C_BG       RGB(243, 244, 248)
#define C_SURFACE  RGB(255, 255, 255)
#define C_BORDER   RGB(209, 213, 219)
#define C_ACCENT   RGB(37,   99, 235)
#define C_ACCENTDK RGB(29,   78, 216)
#define C_TEXT     RGB(17,   24,  39)
#define C_SUBTEXT  RGB(107, 114, 128)
#define C_RUN_OK   RGB(22,  163,  74)
#define C_RUN_OFF  RGB(156, 163, 175)
#define C_DANGER   RGB(220,  38,  38)
#define C_DANGERDK RGB(185,  28,  28)

static HFONT  gUiFont  = NULL;
static HBRUSH gBrBg    = NULL;
static HBRUSH gBrSurf  = NULL;

static void InitTheme(void) {
    if (gUiFont) return;
    LOGFONTW lf = { 0 };
    lf.lfHeight  = -MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72);
    lf.lfWeight  = FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, _countof(lf.lfFaceName), L"Segoe UI");
    gUiFont = CreateFontIndirectW(&lf);
    gBrBg   = CreateSolidBrush(C_BG);
    gBrSurf = CreateSolidBrush(C_SURFACE);
}

static BOOL CALLBACK ApplyFontEnum(HWND child, LPARAM lp) {
    SendMessageW(child, WM_SETFONT, (WPARAM)lp, FALSE);
    return TRUE;
}

// ---- Owner-draw button helper ----
static void DrawThemedButton(DRAWITEMSTRUCT* di) {
    int id = GetDlgCtrlID(di->hwndItem);
    BOOL pressed  = (di->itemState & ODS_SELECTED) != 0;
    BOOL disabled = (di->itemState & ODS_DISABLED)  != 0;

    COLORREF bg, bgdk;
    if (disabled) {
        bg = bgdk = C_SUBTEXT;
    } else if (id == IDC_REMOVE) {
        bg = C_DANGER; bgdk = C_DANGERDK;
    } else {
        bg = C_ACCENT; bgdk = C_ACCENTDK;
    }
    COLORREF fill = pressed ? bgdk : bg;

    HBRUSH br = CreateSolidBrush(fill);
    FillRect(di->hDC, &di->rcItem, br);
    DeleteObject(br);

    SetTextColor(di->hDC, RGB(255, 255, 255));
    SetBkMode(di->hDC, TRANSPARENT);
    HFONT prev = (HFONT)SelectObject(di->hDC, gUiFont);
    wchar_t txt[128];
    GetWindowTextW(di->hwndItem, txt, _countof(txt));
    DrawTextW(di->hDC, txt, -1, &di->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(di->hDC, prev);

    if ((di->itemState & ODS_FOCUS) && !pressed) {
        RECT fr = di->rcItem;
        InflateRect(&fr, -3, -3);
        DrawFocusRect(di->hDC, &fr);
    }
}

// ---- Common color messages (shared by both WndProcs) ----
static LRESULT HandleCtlColor(UINT msg, WPARAM wp, LPARAM lp) {
    HDC hdc = (HDC)wp;
    if (msg == WM_CTLCOLOREDIT) {
        SetTextColor(hdc, C_TEXT);
        SetBkColor(hdc, C_SURFACE);
        return (LRESULT)gBrSurf;
    }
    // WM_CTLCOLORSTATIC and WM_CTLCOLORBTN
    HWND ctrl = (HWND)lp;
    int  id   = GetDlgCtrlID(ctrl);
    SetBkColor(hdc, C_BG);
    if (id == IDC_STATUS) {
        wchar_t t[128]; GetWindowTextW(ctrl, t, _countof(t));
        SetTextColor(hdc, wcsstr(t, L"\x25CF") ? C_RUN_OK : C_SUBTEXT);
    } else if (id == IDC_PATH) {
        SetTextColor(hdc, C_SUBTEXT);
    } else {
        SetTextColor(hdc, C_TEXT);
    }
    return (LRESULT)gBrBg;
}

// ---- Settings window ----
HWND gSettingsWnd = NULL;
static int gSelected = -1;

static LRESULT CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);
static void ShowPresetEditor(HWND parent);   // preset management dialog
static bool gWarnedUnsupported = false;      // warn once per session

// Rebuild the preset combo for a profile selection.
// Adds "(none)" as first entry followed by all named presets.
static void RefreshPresetCombo(HWND wnd, int selectedProfileIdx) {
    HWND cb = GetDlgItem(wnd, IDC_PRESET);
    if (!cb) return;
    ComboBox_ResetContent(cb);
    ComboBox_AddString(cb, L"(none)");
    for (int i = 0; i < gNumPresets; i++)
        ComboBox_AddString(cb, gPresets[i].Name);
    // Select the current profile's preset
    int sel = 0;  // default: (none)
    if (selectedProfileIdx >= 0 && selectedProfileIdx < gNumProfiles) {
        const wchar_t* cur = gProfiles[selectedProfileIdx].DisplayPreset;
        if (cur[0]) {
            for (int i = 0; i < gNumPresets; i++) {
                if (_wcsicmp(gPresets[i].Name, cur) == 0) { sel = i + 1; break; }
            }
        }
    }
    ComboBox_SetCurSel(cb, sel);
    // Gray out if display control is off
    EnableWindow(cb, gDisplayControlEnabled ? TRUE : FALSE);
    EnableWindow(GetDlgItem(wnd, IDC_PRESETEDIT), gDisplayControlEnabled ? TRUE : FALSE);
}

static void RefreshList(HWND wnd) {
    HWND list = GetDlgItem(wnd, IDC_LIST);
    ListView_DeleteAllItems(list);
    for (int i = 0; i < gNumProfiles; i++) {
        LVITEMW it = { 0 };
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i; it.lParam = i;
        it.pszText = gProfiles[i].ProgramExeName;
        ListView_InsertItem(list, &it);
        wchar_t hk[64];
        FormatHotkey(gProfiles[i].HotKey, gProfiles[i].HotKeyModifiers, hk, _countof(hk));
        ListView_SetItemText(list, i, 1, hk);
        ListView_SetItemText(list, i, 2,
            IsExeRunning(gProfiles[i].ProgramExeName) ? L"run" : L"off");
    }
    if (gSelected >= 0 && gSelected < gNumProfiles) {
        ListView_SetItemState(list, gSelected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, gSelected, FALSE);
    }
    CheckDlgButton(wnd, IDC_STARTUP, IsStartupEnabled() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_DISPLAYCTL, gDisplayControlEnabled ? BST_CHECKED : BST_UNCHECKED);
    // Refresh preset combo for current selection
    RefreshPresetCombo(wnd, gSelected);
}

// Enable/disable all profile-specific form controls.
static void EnableProfileControls(HWND wnd, BOOL enable) {
    int ids[] = { IDC_NAME, IDC_HOTKEY, IDC_HIDE, IDC_MIN, IDC_PAUSE,
                  IDC_REMOVE, IDC_STATUS };
    for (int i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++)
        EnableWindow(GetDlgItem(wnd, ids[i]), enable);
    // Preset combo/button also depends on DisplayControl being on
    BOOL presetEnable = enable && gDisplayControlEnabled;
    EnableWindow(GetDlgItem(wnd, IDC_PRESET),     presetEnable);
    EnableWindow(GetDlgItem(wnd, IDC_PRESETEDIT), gDisplayControlEnabled);
}

static void LoadSelectionToFields(HWND wnd, int idx) {
    gSelected = idx;
    if (idx < 0 || idx >= gNumProfiles) {
        SetDlgItemTextW(wnd, IDC_NAME,   L"");
        SetDlgItemTextW(wnd, IDC_PATH,   L"");
        SetDlgItemTextW(wnd, IDC_HOTKEY, L"");
        SetDlgItemTextW(wnd, IDC_STATUS, L"");
        CheckDlgButton(wnd, IDC_HIDE,  BST_UNCHECKED);
        CheckDlgButton(wnd, IDC_MIN,   BST_UNCHECKED);
        CheckDlgButton(wnd, IDC_PAUSE, BST_UNCHECKED);
        CheckDlgButton(wnd, IDC_DISPLAYCTL,
                       gDisplayControlEnabled ? BST_CHECKED : BST_UNCHECKED);
        EnableProfileControls(wnd, FALSE);
        RefreshPresetCombo(wnd, -1);
        return;
    }
    EnableProfileControls(wnd, TRUE);
    PROFILE_CONFIG* p = &gProfiles[idx];
    SetDlgItemTextW(wnd, IDC_NAME, p->ProgramExeName);
    SetDlgItemTextW(wnd, IDC_PATH, p->ProgramPath);
    {
        wchar_t status[128];
        bool running = IsExeRunning(p->ProgramExeName);
        bool present = p->ProgramPath[0] ? (PathFileExistsW(p->ProgramPath) != FALSE) : true;
        swprintf_s(status, _countof(status), L"%s   %s",
            running ? L"\x25CF running" : L"\x25CB not running",
            p->ProgramPath[0] ? (present ? L"| file present" : L"| FILE MISSING") : L"");
        SetDlgItemTextW(wnd, IDC_STATUS, status);
    }
    wchar_t hk[64]; FormatHotkey(p->HotKey, p->HotKeyModifiers, hk, _countof(hk));
    SetDlgItemTextW(wnd, IDC_HOTKEY, hk);
    CheckDlgButton(wnd, IDC_HIDE,  p->HideEnabled     ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_MIN,   p->MinimizeEnabled  ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_PAUSE, p->PauseEnabled     ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_DISPLAYCTL,
                   gDisplayControlEnabled ? BST_CHECKED : BST_UNCHECKED);
    RefreshPresetCombo(wnd, idx);
}

static HWND MakeChild(HWND parent, const wchar_t* cls, const wchar_t* text,
                      DWORD style, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
}

static HWND MakeBtn(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id) {
    return MakeChild(parent, L"BUTTON", text, BS_OWNERDRAW, x, y, w, h, id);
}

// ---- Hotkey edit subclass: captures key combos and formats them as text ----
static LRESULT CALLBACK HotkeyEditSubclass(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR subId, DWORD_PTR refData) {
    (void)subId; (void)refData;
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            WORD vk = (WORD)wp;
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                vk == VK_LWIN  || vk == VK_RWIN)
                return 0;
            if (vk == VK_TAB || vk == VK_ESCAPE)
                return DefSubclassProc(hWnd, msg, wp, lp);
            if (vk == VK_BACK || vk == VK_DELETE) {
                SetWindowTextW(hWnd, L"");
                return 0;
            }
            BOOL shift = GetKeyState(VK_SHIFT)   & 0x8000;
            BOOL ctrl  = GetKeyState(VK_CONTROL) & 0x8000;
            BOOL alt   = GetKeyState(VK_MENU)    & 0x8000;
            UINT mods  = (shift ? MOD_SHIFT   : 0)
                       | (ctrl  ? MOD_CONTROL : 0)
                       | (alt   ? MOD_ALT     : 0);
            wchar_t buf[64];
            FormatHotkey((u32)vk, mods, buf, _countof(buf));
            if (buf[0]) SetWindowTextW(hWnd, buf);
            return 0;
        }
        case WM_CHAR:
        case WM_SYSCHAR:
            return 0;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hWnd, HotkeyEditSubclass, 0);
            break;
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// Layout constants
#define M   16     // outer margin
#define LW  300    // list width
#define LH  374    // list height
#define RX  330    // right panel left edge
#define LBW  65    // label column width
#define FX  399    // form field left edge (RX + LBW + 4)
#define FW  243    // form field width
#define RPW 312    // full right panel width (FX + FW - RX)

static void CreateControls(HWND wnd) {
    // --- List (left panel) ---
    HWND list = MakeChild(wnd, WC_LISTVIEWW, L"",
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER,
        M, M, LW, LH, IDC_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(list, L"Explorer", NULL);
    ListView_SetBkColor(list, C_SURFACE);
    ListView_SetTextColor(list, C_TEXT);
    ListView_SetTextBkColor(list, C_SURFACE);
    LVCOLUMNW c = { 0 }; c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.pszText = L"Program"; c.cx = 150; ListView_InsertColumn(list, 0, &c);
    c.pszText = L"Hotkey";  c.cx = 90;  ListView_InsertColumn(list, 1, &c);
    c.pszText = L"Status";  c.cx = 55;  ListView_InsertColumn(list, 2, &c);

    MakeBtn(wnd, L"+ Add from running...", M,         M+LH+8, 145, 28, IDC_ADD);
    MakeBtn(wnd, L"Remove",               M+145+6,    M+LH+8,  80, 28, IDC_REMOVE);

    // --- Right panel: form fields ---
    int ry = M;
    MakeChild(wnd, L"STATIC", L"Name:",   SS_RIGHT, RX, ry+4,  LBW, 20, 0);
    MakeChild(wnd, L"EDIT",   L"",        WS_BORDER|ES_AUTOHSCROLL, FX, ry, FW, 26, IDC_NAME);
    ry += 36;

    MakeChild(wnd, L"STATIC", L"Path:",   SS_RIGHT, RX, ry+2,  LBW, 20, 0);
    MakeChild(wnd, L"STATIC", L"",        SS_LEFTNOWORDWRAP, FX, ry, FW, 34, IDC_PATH);
    ry += 44;

    MakeChild(wnd, L"STATIC", L"Hotkey:", SS_RIGHT, RX, ry+4, LBW, 20, 0);
    HWND hkEdit = MakeChild(wnd, L"EDIT", L"", WS_BORDER|ES_AUTOHSCROLL, FX, ry, FW, 26, IDC_HOTKEY);
    SetWindowSubclass(hkEdit, HotkeyEditSubclass, 0, 0);
    ry += 36;

    MakeChild(wnd, L"BUTTON", L"Hide / show",        BS_AUTOCHECKBOX, FX, ry, FW, 22, IDC_HIDE);
    ry += 26;
    MakeChild(wnd, L"BUTTON", L"Minimize / restore", BS_AUTOCHECKBOX, FX, ry, FW, 22, IDC_MIN);
    ry += 26;
    MakeChild(wnd, L"BUTTON", L"Pause / resume",     BS_AUTOCHECKBOX, FX, ry, FW, 22, IDC_PAUSE);
    ry += 32;

    // --- Display preset row ---
    MakeChild(wnd, L"STATIC", L"Display:", SS_RIGHT | SS_CENTERIMAGE,
              RX, ry+2, LBW, 20, IDC_PRESETLBL);
    // Preset combo: narrower to leave room for Edit button
    MakeChild(wnd, L"COMBOBOX", L"",
              CBS_DROPDOWNLIST | WS_VSCROLL,
              FX, ry, FW - 72, 200, IDC_PRESET);
    MakeBtn(wnd, L"Edit...", FX + FW - 68, ry, 68, 26, IDC_PRESETEDIT);
    ry += 34;

    MakeChild(wnd, L"STATIC", L"", SS_LEFTNOWORDWRAP, RX, ry, RPW, 36, IDC_STATUS);
    ry += 52;

    // --- Global controls ---
    MakeChild(wnd, L"BUTTON", L"Control monitor brightness (DDC/CI)",
              BS_AUTOCHECKBOX, RX, ry, RPW, 22, IDC_DISPLAYCTL);
    ry += 28;

    MakeChild(wnd, L"BUTTON", L"Run at startup", BS_AUTOCHECKBOX, RX, ry, RPW, 22, IDC_STARTUP);
    ry += 36;

    MakeBtn(wnd, L"Open INI folder",              RX,         ry, 151, 28, IDC_OPENINI);
    MakeBtn(wnd, L"Install to user programs...",  RX+151+5,   ry, 156, 28, IDC_INSTALL);
}

static void ApplyFieldsToSelection(HWND wnd) {
    if (gSelected < 0 || gSelected >= gNumProfiles) return;
    PROFILE_CONFIG* p = &gProfiles[gSelected];

    GetDlgItemTextW(wnd, IDC_NAME, p->ProgramExeName, MAX_NAME);

    wchar_t hk[64]; GetDlgItemTextW(wnd, IDC_HOTKEY, hk, _countof(hk));
    u32 vk = 0; UINT mods = 0;
    if (hk[0] && ParseHotkey(hk, &vk, &mods)) {
        p->HotKey = vk; p->HotKeyModifiers = mods;
    } else if (hk[0]) {
        MessageBoxW(wnd, L"Invalid hotkey. Example: Ctrl+Alt+V", APPNAME, MB_OK | MB_ICONWARNING);
        FormatHotkey(p->HotKey, p->HotKeyModifiers, hk, _countof(hk));
        SetDlgItemTextW(wnd, IDC_HOTKEY, hk);
    }

    p->HideEnabled     = IsDlgButtonChecked(wnd, IDC_HIDE)  == BST_CHECKED;
    p->MinimizeEnabled = IsDlgButtonChecked(wnd, IDC_MIN)   == BST_CHECKED;
    p->PauseEnabled    = IsDlgButtonChecked(wnd, IDC_PAUSE) == BST_CHECKED;

    // Save display preset selection
    HWND cb = GetDlgItem(wnd, IDC_PRESET);
    if (cb) {
        int sel = ComboBox_GetCurSel(cb);
        if (sel <= 0) {
            p->DisplayPreset[0] = L'\0';
        } else {
            // sel==0 is "(none)"; sel 1..N maps to gPresets[sel-1]
            int pi = sel - 1;
            if (pi >= 0 && pi < gNumPresets)
                wcscpy_s(p->DisplayPreset, MAX_NAME, gPresets[pi].Name);
        }
    }

    SaveConfig();
    EnterCriticalSection(&gHotkeyLock);
    RebuildHotkeys();
    LeaveCriticalSection(&gHotkeyLock);
    RefreshList(wnd);
}

// ---- Preset editor -------------------------------------------------------
// Simple modal dialog: list of presets on the left, edit fields on the right.
// Supports: add preset, delete preset, edit brightness/contrast/colortemp,
// capture current monitor values.

static int   gPeSelected = -1;   // selected preset index in the editor
static bool  gPeDone = false;    // set by WM_DESTROY to unblock ShowPresetEditor

// Display order for VCP 0x14 codes in the CT dropdown: warmest first.
// Only codes present in gPrimaryVcp14Vals are added to the combo.
static const BYTE kVcp14DisplayOrder[] = {
    0x0C,  // User Color (warm custom)
    0x0B,  // Custom Color
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,  // 4000K..11500K
    0x01, 0x02,   // sRGB, Native
};

// Populate the CT combo from gPrimaryVcp14Vals.
// Item 0 is always "(don't change)" with data PRESET_UNSET.
// Remaining items use CB_SETITEMDATA to store the VCP code (1-12).
static void BuildCtCombo(HWND cb) {
    ComboBox_ResetContent(cb);
    int idx = ComboBox_AddString(cb, L"(don't change)");
    SendMessage(cb, CB_SETITEMDATA, idx, (LPARAM)PRESET_UNSET);

    if (gPrimaryVcp14Count <= 0) return;  // monitor not detected yet

    for (int oi = 0; oi < (int)(sizeof(kVcp14DisplayOrder)); oi++) {
        BYTE code = kVcp14DisplayOrder[oi];
        // Check if this code is in the monitor's list
        bool supported = false;
        for (int vi = 0; vi < gPrimaryVcp14Count; vi++)
            if (gPrimaryVcp14Vals[vi] == code) { supported = true; break; }
        if (!supported) continue;
        idx = ComboBox_AddString(cb, GetVcp14Label(code));
        SendMessage(cb, CB_SETITEMDATA, idx, (LPARAM)(INT_PTR)(int)code);
    }
}

// Update the checkbox label text to show the current slider value.
static void PeUpdateLabels(HWND wnd) {
    int b = (int)SendDlgItemMessage(wnd, IDC_PE_BRIGHT, TBM_GETPOS, 0, 0);
    int c = (int)SendDlgItemMessage(wnd, IDC_PE_CONT,   TBM_GETPOS, 0, 0);
    wchar_t buf[64];
    swprintf_s(buf, _countof(buf), L"Brightness: %d%%", b);
    SetDlgItemTextW(wnd, IDC_PE_BRIGHT_CHK, buf);
    swprintf_s(buf, _countof(buf), L"Contrast: %d%%", c);
    SetDlgItemTextW(wnd, IDC_PE_CONT_CHK, buf);
}

static void PeRefreshList(HWND wnd) {
    HWND lb = GetDlgItem(wnd, IDC_LIST);
    int prev = ListBox_GetCurSel(lb);
    ListBox_ResetContent(lb);
    for (int i = 0; i < gNumPresets; i++)
        ListBox_AddString(lb, gPresets[i].Name);
    if (gNumPresets == 0) { gPeSelected = -1; return; }
    int sel = (prev >= 0 && prev < gNumPresets) ? prev : 0;
    ListBox_SetCurSel(lb, sel);
    gPeSelected = sel;
}

static void PeLoadFields(HWND wnd, int idx) {
    gPeSelected = idx;
    if (idx < 0 || idx >= gNumPresets) {
        SetDlgItemTextW(wnd, IDC_PE_NAME, L"");
        SendDlgItemMessage(wnd, IDC_PE_BRIGHT, TBM_SETPOS, TRUE, 50);
        SendDlgItemMessage(wnd, IDC_PE_CONT,   TBM_SETPOS, TRUE, 50);
        CheckDlgButton(wnd, IDC_PE_BRIGHT_CHK, BST_UNCHECKED);
        CheckDlgButton(wnd, IDC_PE_CONT_CHK,   BST_UNCHECKED);
        CheckDlgButton(wnd, IDC_PE_CTEMP_CHK,  BST_UNCHECKED);
        ComboBox_SetCurSel(GetDlgItem(wnd, IDC_PE_CTEMP), 0);
        PeUpdateLabels(wnd);
        return;
    }
    DISPLAY_PRESET* p = &gPresets[idx];
    SetDlgItemTextW(wnd, IDC_PE_NAME, p->Name);
    if (p->Brightness != PRESET_UNSET) {
        SendDlgItemMessage(wnd, IDC_PE_BRIGHT, TBM_SETPOS, TRUE, p->Brightness);
        CheckDlgButton(wnd, IDC_PE_BRIGHT_CHK, BST_CHECKED);
    } else {
        SendDlgItemMessage(wnd, IDC_PE_BRIGHT, TBM_SETPOS, TRUE, 50);
        CheckDlgButton(wnd, IDC_PE_BRIGHT_CHK, BST_UNCHECKED);
    }
    if (p->Contrast != PRESET_UNSET) {
        SendDlgItemMessage(wnd, IDC_PE_CONT, TBM_SETPOS, TRUE, p->Contrast);
        CheckDlgButton(wnd, IDC_PE_CONT_CHK, BST_CHECKED);
    } else {
        SendDlgItemMessage(wnd, IDC_PE_CONT, TBM_SETPOS, TRUE, 50);
        CheckDlgButton(wnd, IDC_PE_CONT_CHK, BST_UNCHECKED);
    }
    // Find the combo item whose CB_GETITEMDATA matches p->ColorTemp.
    {
        HWND ctCb = GetDlgItem(wnd, IDC_PE_CTEMP);
        int ctSel = 0;  // default: index 0 = "(don't change)"
        if (p->ColorTemp != PRESET_UNSET) {
            int cnt = ComboBox_GetCount(ctCb);
            for (int i = 1; i < cnt; i++) {
                LRESULT data = SendMessage(ctCb, CB_GETITEMDATA, (WPARAM)i, 0);
                if ((int)data == p->ColorTemp) { ctSel = i; break; }
            }
        }
        ComboBox_SetCurSel(ctCb, ctSel);
        CheckDlgButton(wnd, IDC_PE_CTEMP_CHK,
                       (p->ColorTemp != PRESET_UNSET && ctSel > 0)
                           ? BST_CHECKED : BST_UNCHECKED);
    }
    // Monitor Preset label
    if (p->ProfileMode != PRESET_UNSET) {
        wchar_t pmBuf[64];
        swprintf_s(pmBuf, _countof(pmBuf), L"%s (0x%02X)",
                   GetVcpF0Label((BYTE)p->ProfileMode), (BYTE)p->ProfileMode);
        SetDlgItemTextW(wnd, IDC_PE_PROFILE_LBL, pmBuf);
    } else {
        SetDlgItemTextW(wnd, IDC_PE_PROFILE_LBL, L"(not captured)");
    }
    PeUpdateLabels(wnd);
}

static void PeSaveFields(HWND wnd) {
    if (gPeSelected < 0 || gPeSelected >= gNumPresets) return;
    DISPLAY_PRESET* p = &gPresets[gPeSelected];
    GetDlgItemTextW(wnd, IDC_PE_NAME, p->Name, MAX_NAME);
    if (IsDlgButtonChecked(wnd, IDC_PE_BRIGHT_CHK) == BST_CHECKED) {
        p->Brightness = (int)SendDlgItemMessage(wnd, IDC_PE_BRIGHT, TBM_GETPOS, 0, 0);
    } else {
        p->Brightness = PRESET_UNSET;
    }
    if (IsDlgButtonChecked(wnd, IDC_PE_CONT_CHK) == BST_CHECKED) {
        p->Contrast = (int)SendDlgItemMessage(wnd, IDC_PE_CONT, TBM_GETPOS, 0, 0);
    } else {
        p->Contrast = PRESET_UNSET;
    }
    if (IsDlgButtonChecked(wnd, IDC_PE_CTEMP_CHK) == BST_CHECKED) {
        HWND ctCb = GetDlgItem(wnd, IDC_PE_CTEMP);
        int sel = ComboBox_GetCurSel(ctCb);
        if (sel > 0) {
            LRESULT data = SendMessage(ctCb, CB_GETITEMDATA, (WPARAM)sel, 0);
            p->ColorTemp = (data != CB_ERR) ? (int)data : PRESET_UNSET;
        } else {
            p->ColorTemp = PRESET_UNSET;
        }
    } else {
        p->ColorTemp = PRESET_UNSET;
    }
}

static LRESULT CALLBACK PresetEditorProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            // Warn about unsupported monitors (once per session)
            if (!gWarnedUnsupported) {
                wchar_t names[8][128];
                int n = DisplayListUnsupported(names, 8);
                if (n > 0) {
                    wchar_t buf[2048] = L"The following monitors do not support DDC/CI\n"
                                        L"(or DDC/CI is disabled in their OSD menu).\n"
                                        L"Display control will not work for them:\n\n";
                    for (int i = 0; i < n; i++) {
                        wcscat_s(buf, _countof(buf), L"  \x2022 ");
                        wcscat_s(buf, _countof(buf), names[i]);
                        wcscat_s(buf, _countof(buf), L"\n");
                    }
                    wcscat_s(buf, _countof(buf),
                             L"\nYou may need to enable DDC/CI in the monitor's OSD settings.");
                    MessageBoxW(wnd, buf, APPNAME L" - Monitor Compatibility",
                                MB_OK | MB_ICONINFORMATION);
                    gWarnedUnsupported = true;
                }
            }

            // Layout
            // Left panel: preset list + add/delete buttons
            // Right panel: name, brightness(chk+slider), contrast(chk+slider),
            //              colortemp(chk+combo), capture, apply, close/cancel
            int lx = 12, ly = 12, lw = 150;
            int ex = lx + lw + 14;   // right panel left edge
            int ew = 244;            // right panel width
            int ey = ly;

            // Preset list — height will match right panel, set after layout
            // (placeholder; actual lh calculated below)

            // Name row
            MakeChild(wnd, L"STATIC", L"Name:", SS_RIGHT|SS_CENTERIMAGE,
                ex, ey+2, 44, 22, 0);
            MakeChild(wnd, L"EDIT", L"", WS_BORDER|ES_AUTOHSCROLL,
                ex+48, ey, ew-48, 26, IDC_PE_NAME);
            ey += 32;

            // Brightness: checkbox row, then slider row
            MakeChild(wnd, L"BUTTON", L"Brightness",
                BS_AUTOCHECKBOX, ex, ey, 100, 22, IDC_PE_BRIGHT_CHK);
            ey += 24;
            {
                HWND sl = MakeChild(wnd, TRACKBAR_CLASSW, L"",
                    TBS_HORZ|TBS_NOTICKS|TBS_TOOLTIPS,
                    ex, ey, ew, 24, IDC_PE_BRIGHT);
                SendMessage(sl, TBM_SETRANGE,   TRUE, MAKELPARAM(0, 100));
                SendMessage(sl, TBM_SETPOS,     TRUE, 50);
                SendMessage(sl, TBM_SETPAGESIZE, 0,   5);
            }
            ey += 30;

            // Contrast: checkbox row, then slider row
            MakeChild(wnd, L"BUTTON", L"Contrast",
                BS_AUTOCHECKBOX, ex, ey, 100, 22, IDC_PE_CONT_CHK);
            ey += 24;
            {
                HWND sl = MakeChild(wnd, TRACKBAR_CLASSW, L"",
                    TBS_HORZ|TBS_NOTICKS|TBS_TOOLTIPS,
                    ex, ey, ew, 24, IDC_PE_CONT);
                SendMessage(sl, TBM_SETRANGE,   TRUE, MAKELPARAM(0, 100));
                SendMessage(sl, TBM_SETPOS,     TRUE, 50);
                SendMessage(sl, TBM_SETPAGESIZE, 0,   5);
            }
            ey += 30;

            // Color temperature: checkbox + combo on same row
            MakeChild(wnd, L"BUTTON", L"Color temp:",
                BS_AUTOCHECKBOX, ex, ey+2, 90, 22, IDC_PE_CTEMP_CHK);
            HWND ctCombo = MakeChild(wnd, L"COMBOBOX", L"",
                CBS_DROPDOWNLIST|WS_VSCROLL, ex+94, ey, ew-94, 200, IDC_PE_CTEMP);
            BuildCtCombo(ctCombo);
            ComboBox_SetCurSel(ctCombo, 0);
            ey += 32;

            // Monitor Preset (VCP 0xF0): label + read-only display
            MakeChild(wnd, L"STATIC", L"Monitor preset:",
                SS_LEFT, ex, ey+3, 90, 18, 0);
            MakeChild(wnd, L"STATIC", L"(use Capture)",
                SS_LEFT|SS_SUNKEN, ex+94, ey, ew-94, 24, IDC_PE_PROFILE_LBL);
            ey += 30;

            // Capture full-width
            MakeBtn(wnd, L"Capture from monitor", ex, ey, ew, 26, IDC_PE_CAPTURE);
            ey += 34;

            // Apply full-width (accent color via DrawThemedButton)
            MakeBtn(wnd, L"Apply to monitor", ex, ey, ew, 26, IDC_PE_APPLY);
            ey += 34;

            // Close / Cancel split
            MakeBtn(wnd, L"Close",  ex,              ey, (ew-6)/2, 28, IDC_PE_OK);
            MakeBtn(wnd, L"Cancel", ex+(ew-6)/2+6,   ey, (ew-6)/2, 28, IDC_PE_CANCEL);
            ey += 28;

            // Now create the list to match the right panel height
            int lh = ey - ly - 34;  // leave room for Add/Delete below
            MakeChild(wnd, L"LISTBOX", L"",
                LBS_NOTIFY | WS_BORDER | WS_VSCROLL, lx, ly, lw, lh, IDC_LIST);
            MakeBtn(wnd, L"+ Add",  lx,            ly+lh+6, (lw-4)/2, 26, IDC_ADD);
            MakeBtn(wnd, L"Delete", lx+(lw-4)/2+4, ly+lh+6, (lw-4)/2, 26, IDC_REMOVE);

            EnumChildWindows(wnd, ApplyFontEnum, (LPARAM)gUiFont);
            PeRefreshList(wnd);
            PeLoadFields(wnd, gPeSelected >= 0 ? gPeSelected : 0);
            return 0;
        }
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wp; RECT rc; GetClientRect(wnd, &rc);
            FillRect(hdc, &rc, gBrBg); return 1;
        }
        case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORBTN:
            return HandleCtlColor(msg, wp, lp);
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
            if (di->CtlType == ODT_BUTTON) DrawThemedButton(di);
            return TRUE;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp), code = HIWORD(wp);
            // Auto-save name on focus-out
            if (id == IDC_PE_NAME && code == EN_KILLFOCUS) {
                PeSaveFields(wnd);
                PeRefreshList(wnd);
            }
            if ((id == IDC_PE_BRIGHT_CHK || id == IDC_PE_CONT_CHK ||
                 id == IDC_PE_CTEMP_CHK) && code == BN_CLICKED) {
                PeSaveFields(wnd);
            }
            if (id == IDC_PE_CTEMP && code == CBN_SELCHANGE) {
                PeSaveFields(wnd);
            }
            if (id == IDC_LIST && code == LBN_SELCHANGE) {
                int sel = ListBox_GetCurSel(GetDlgItem(wnd, IDC_LIST));
                if (sel >= 0) PeLoadFields(wnd, sel);
            }
            if (id == IDC_ADD && code == BN_CLICKED) {
                // Create a new preset with a default name
                int idx = GetOrCreatePreset(L"New Preset");
                if (idx >= 0) { SaveConfig(); PeRefreshList(wnd); PeLoadFields(wnd, idx); }
            }
            if (id == IDC_REMOVE && code == BN_CLICKED) {
                if (gPeSelected >= 0 && gPeSelected < gNumPresets) {
                    memmove(&gPresets[gPeSelected], &gPresets[gPeSelected+1],
                            (gNumPresets - gPeSelected - 1) * sizeof(DISPLAY_PRESET));
                    gNumPresets--;
                    SaveConfig();
                    PeRefreshList(wnd);
                    PeLoadFields(wnd, gPeSelected >= gNumPresets ?
                                 gNumPresets-1 : gPeSelected);
                }
            }
            if (id == IDC_PE_CAPTURE && code == BN_CLICKED) {
                // Capture current monitor values into the selected preset.
                // DDC/CI takes ~150-300ms; disable form + show wait cursor.
                if (gPeSelected >= 0 && gPeSelected < gNumPresets) {
                    EnableWindow(wnd, FALSE);
                    HCURSOR hOld = SetCursor(LoadCursorW(NULL, IDC_WAIT));
                    DISPLAY_PRESET captured;
                    bool captureOk = DisplayCaptureCurrent(NULL, &captured);
                    SetCursor(hOld);
                    EnableWindow(wnd, TRUE);
                    SetForegroundWindow(wnd);
                    if (captureOk) {
                        // Only overwrite fields that were successfully captured
                        DISPLAY_PRESET* p = &gPresets[gPeSelected];
                        if (captured.Brightness != PRESET_UNSET) {
                            p->Brightness = captured.Brightness;
                            CheckDlgButton(wnd, IDC_PE_BRIGHT_CHK, BST_CHECKED);
                        }
                        if (captured.Contrast != PRESET_UNSET) {
                            p->Contrast = captured.Contrast;
                            CheckDlgButton(wnd, IDC_PE_CONT_CHK, BST_CHECKED);
                        }
                        if (captured.ColorTemp != PRESET_UNSET) {
                            p->ColorTemp = captured.ColorTemp;
                            CheckDlgButton(wnd, IDC_PE_CTEMP_CHK, BST_CHECKED);
                        }
                        if (captured.ProfileMode != PRESET_UNSET) {
                            p->ProfileMode = captured.ProfileMode;
                        }
                        PeLoadFields(wnd, gPeSelected);
                        SaveConfig();
                        MessageBoxW(wnd, L"Current monitor values captured into preset.",
                                    APPNAME, MB_OK | MB_ICONINFORMATION);
                    } else {
                        MessageBoxW(wnd, L"Could not read monitor values.\n"
                                    L"Make sure DDC/CI is enabled in the monitor OSD"
                                    L" and a DDC/CI-capable monitor is connected.",
                                    APPNAME L" - Capture Failed",
                                    MB_OK | MB_ICONWARNING);
                    }
                }
            }
            if (id == IDC_PE_APPLY && code == BN_CLICKED) {
                CrashLog("[ui] Apply clicked gPeSelected=%d gNumPresets=%d\n",
                         gPeSelected, gNumPresets);
                PeSaveFields(wnd);
                if (gPeSelected >= 0 && gPeSelected < gNumPresets) {
                    CrashLog("[ui] preset B=%d C=%d CT=%d\n",
                             gPresets[gPeSelected].Brightness,
                             gPresets[gPeSelected].Contrast,
                             gPresets[gPeSelected].ColorTemp);
                    // Always route through the worker thread so DDC/CI never
                    // runs on the UI thread (concurrent I2C access causes crash).
                    // force=1 bypasses the gDisplayControlEnabled gate.
                    EnterCriticalSection(&gHotkeyLock);
                    gDesiredDisplay.hwnd  = wnd;   // apply to this dialog's monitor
                    gDesiredDisplay.valid = true;
                    wcscpy_s(gDesiredDisplay.presetName, MAX_NAME,
                             gPresets[gPeSelected].Name);
                    LeaveCriticalSection(&gHotkeyLock);
                    Job j = { JOB_APPLY_DISPLAY, 1 };  // 1 = force
                    CrashLog("[ui] pushing JOB_APPLY_DISPLAY force=1\n");
                    JobQueuePush(j);
                    CrashLog("[ui] job pushed ok\n");
                }
            }
            if (id == IDC_PE_OK && code == BN_CLICKED) {
                PeSaveFields(wnd);
                SaveConfig();
                DestroyWindow(wnd);
            }
            if (id == IDC_PE_CANCEL && code == BN_CLICKED) {
                DestroyWindow(wnd);
            }
            return 0;
        }
        case WM_HSCROLL: {
            // Trackbar (slider) moved — keep gPresets in sync and update label.
            int id = GetDlgCtrlID((HWND)lp);
            if (id == IDC_PE_BRIGHT || id == IDC_PE_CONT) {
                PeSaveFields(wnd);
                PeUpdateLabels(wnd);
            }
            return 0;
        }
        case WM_CLOSE:   DestroyWindow(wnd); return 0;
        case WM_DESTROY: gPeDone = true; return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static void ShowPresetEditor(HWND parent) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc   = PresetEditorProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = L"UAC_PresetEditorWnd";
        wc.hbrBackground = NULL;
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassW(&wc);
        reg = true;
    }
    gPeDone = false;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT r = { 0, 0, 440, 370 };
    AdjustWindowRectEx(&r, style, FALSE, 0);
    HWND wnd = CreateWindowExW(0, L"UAC_PresetEditorWnd",
        APPNAME L" - Edit Display Presets",
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right-r.left, r.bottom-r.top,
        parent, NULL, GetModuleHandleW(NULL), NULL);
    ShowWindow(wnd, SW_SHOW);
    EnableWindow(parent, FALSE);
    MSG m;
    while (!gPeDone && GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(wnd, &m)) {
            TranslateMessage(&m); DispatchMessageW(&m);
        }
        if (!IsWindow(wnd)) break;
    }
    if (IsWindow(wnd)) DestroyWindow(wnd);
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}

static LRESULT CALLBACK SettingsProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            CreateControls(wnd);
            EnumChildWindows(wnd, ApplyFontEnum, (LPARAM)gUiFont);
            RefreshList(wnd);
            EnableProfileControls(wnd, FALSE);  // nothing selected yet
            return 0;

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wp;
            RECT rc; GetClientRect(wnd, &rc);
            FillRect(hdc, &rc, gBrBg);
            // subtle vertical separator between list and form
            HPEN pen = CreatePen(PS_SOLID, 1, C_BORDER);
            HPEN old = (HPEN)SelectObject(hdc, pen);
            int sx = M + LW + 7;
            MoveToEx(hdc, sx, M,      NULL);
            LineTo  (hdc, sx, M+LH);
            SelectObject(hdc, old);
            DeleteObject(pen);
            return 1;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
            return HandleCtlColor(msg, wp, lp);

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
            if (di->CtlType == ODT_BUTTON) DrawThemedButton(di);
            return TRUE;
        }

        case WM_NOTIFY: {
            LPNMHDR nh = (LPNMHDR)lp;
            if (nh->idFrom == IDC_LIST) {
                if (nh->code == NM_CUSTOMDRAW) {
                    LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
                    switch (cd->nmcd.dwDrawStage) {
                        case CDDS_PREPAINT:    return CDRF_NOTIFYITEMDRAW;
                        case CDDS_ITEMPREPAINT: {
                            BOOL sel = (ListView_GetItemState(nh->hwndFrom,
                                (int)cd->nmcd.dwItemSpec, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                            cd->clrText   = sel ? RGB(255,255,255) : C_TEXT;
                            cd->clrTextBk = sel ? C_ACCENT : C_SURFACE;
                            return CDRF_DODEFAULT;
                        }
                        default: return CDRF_DODEFAULT;
                    }
                }
                if (nh->code == LVN_ITEMCHANGED) {
                    LPNMLISTVIEW nv = (LPNMLISTVIEW)lp;
                    if ((nv->uChanged & LVIF_STATE) && (nv->uNewState & LVIS_SELECTED))
                        LoadSelectionToFields(wnd, (int)nv->lParam);
                }
            }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wp), code = HIWORD(wp);
            switch (id) {
                case IDC_NAME:
                case IDC_HOTKEY:
                    if (code == EN_KILLFOCUS) ApplyFieldsToSelection(wnd);
                    break;
                case IDC_HIDE: case IDC_MIN: case IDC_PAUSE:
                    if (code == BN_CLICKED) ApplyFieldsToSelection(wnd);
                    break;
                case IDC_REMOVE:
                    if (gSelected >= 0 && gSelected < gNumProfiles) {
                        memmove(&gProfiles[gSelected], &gProfiles[gSelected + 1],
                                (gNumProfiles - gSelected - 1) * sizeof(PROFILE_CONFIG));
                        gNumProfiles--;
                        gSelected = -1;
                        SaveConfig();
                        EnterCriticalSection(&gHotkeyLock);
                        RebuildHotkeys();
                        LeaveCriticalSection(&gHotkeyLock);
                        RefreshList(wnd);
                        LoadSelectionToFields(wnd, -1);
                    }
                    break;
                case IDC_STARTUP:
                    if (code == BN_CLICKED)
                        SetStartupEnabled(IsDlgButtonChecked(wnd, IDC_STARTUP) == BST_CHECKED);
                    break;
                case IDC_DISPLAYCTL:
                    if (code == BN_CLICKED) {
                        gDisplayControlEnabled =
                            (IsDlgButtonChecked(wnd, IDC_DISPLAYCTL) == BST_CHECKED)
                            ? TRUE : FALSE;
                        SaveConfig();
                        // Re-evaluate enabled state for preset controls
                        if (gSelected >= 0)
                            EnableProfileControls(wnd, TRUE);
                    }
                    break;
                case IDC_PRESET:
                    if (code == CBN_SELCHANGE) ApplyFieldsToSelection(wnd);
                    break;
                case IDC_PRESETEDIT:
                    ShowPresetEditor(wnd);
                    // Refresh combo after editing (presets may have changed)
                    RefreshPresetCombo(wnd, gSelected);
                    break;
                case IDC_OPENINI:  OpenConfigFolder(); break;
                case IDC_INSTALL:  InstallToUserPrograms(wnd); break;
                case IDC_ADD: {
                    wchar_t name[MAX_NAME], path[MAX_PATH];
                    if (PickRunningProcess(wnd, name, MAX_NAME, path, MAX_PATH)
                        && gNumProfiles < MAX_PROFILES) {
                        EnterCriticalSection(&gHotkeyLock);
                        int idx = gNumProfiles++;
                        memset(&gProfiles[idx], 0, sizeof(gProfiles[idx]));
                        wcscpy_s(gProfiles[idx].ProgramExeName, MAX_NAME, name);
                        wcscpy_s(gProfiles[idx].ProgramPath,    MAX_PATH, path);
                        LeaveCriticalSection(&gHotkeyLock);
                        SaveConfig();
                        EnterCriticalSection(&gHotkeyLock);
                        RebuildHotkeys();
                        LeaveCriticalSection(&gHotkeyLock);
                        RefreshList(wnd);
                    }
                    break;
                }
            }
            return 0;
        }

        case WM_CLOSE:   DestroyWindow(wnd); return 0;
        case WM_DESTROY: gSettingsWnd = NULL; return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void ShowSettingsWindow(HINSTANCE inst, HWND owner) {
    if (gSettingsWnd) { SetForegroundWindow(gSettingsWnd); return; }

    InitTheme();

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = SettingsProc;
        wc.hInstance     = inst;
        wc.lpszClassName = L"UAC_SettingsWnd";
        wc.hbrBackground = NULL;   // handled in WM_ERASEBKGND
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(IDI_ICON1));
        wc.hIconSm       = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_ICON1),
                               IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        RegisterClassExW(&wc);
        registered = true;
    }

    {
        DWORD winStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT wrc = { 0, 0, FX + FW + M, M + LH + 8 + 28 + M + 60 };  // +60 for display rows
        AdjustWindowRectEx(&wrc, winStyle, FALSE, 0);
        gSettingsWnd = CreateWindowExW(0, L"UAC_SettingsWnd", APPNAME L" v" VERSION L" - Settings",
            winStyle, CW_USEDEFAULT, CW_USEDEFAULT,
            wrc.right - wrc.left, wrc.bottom - wrc.top,
            owner, NULL, inst, NULL);
    }
    ShowWindow(gSettingsWnd, SW_SHOW);
}

// ---- Process picker ----
typedef struct {
    wchar_t name[MAX_NAME];
    wchar_t path[MAX_PATH];
    int     iconIdx;
} PickRow;
static PickRow gPickRows[2048];
static int     gPickCount  = 0;
static int     gPickResult = -1;
static bool    gPickDone   = false;

static int ComparePickRows(const void* a, const void* b) {
    return _wcsicmp(((const PickRow*)a)->name, ((const PickRow*)b)->name);
}

static void FillProcessList(HWND list) {
    gPickCount = 0;
    HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16);
    ListView_SetImageList(list, il, LVSIL_SMALL);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            if (gPickCount >= 2048) break;
            PickRow* r = &gPickRows[gPickCount];
            wcscpy_s(r->name, MAX_NAME, pe.szExeFile);
            r->path[0] = 0;
            r->iconIdx = -1;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (h) {
                DWORD cb = MAX_PATH;
                QueryFullProcessImageNameW(h, 0, r->path, &cb);
                CloseHandle(h);
            }
            if (r->path[0]) {
                SHFILEINFOW sfi = { 0 };
                if (SHGetFileInfoW(r->path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON)) {
                    r->iconIdx = ImageList_AddIcon(il, sfi.hIcon);
                    DestroyIcon(sfi.hIcon);
                }
            }
            gPickCount++;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    qsort(gPickRows, gPickCount, sizeof(PickRow), ComparePickRows);

    // Deduplicate by exe name; prefer entries that have a resolved path/icon
    int n = 0;
    for (int i = 0; i < gPickCount; i++) {
        if (n == 0 || _wcsicmp(gPickRows[i].name, gPickRows[n-1].name) != 0) {
            if (n != i) gPickRows[n] = gPickRows[i];
            n++;
        } else if (!gPickRows[n-1].path[0] && gPickRows[i].path[0]) {
            gPickRows[n-1] = gPickRows[i];
        }
    }
    gPickCount = n;
}

static void ApplyFilter(HWND wnd, const wchar_t* filter) {
    HWND list = GetDlgItem(wnd, IDC_PICKLIST);
    ListView_DeleteAllItems(list);
    int row = 0;
    for (int i = 0; i < gPickCount; i++) {
        if (!filter[0] || StrStrIW(gPickRows[i].name, filter) || StrStrIW(gPickRows[i].path, filter)) {
            LVITEMW it = { 0 };
            it.mask    = LVIF_TEXT | LVIF_PARAM | (gPickRows[i].iconIdx >= 0 ? LVIF_IMAGE : 0);
            it.iItem   = row++;
            it.lParam  = i;
            it.iImage  = gPickRows[i].iconIdx;
            it.pszText = gPickRows[i].name;
            int r = ListView_InsertItem(list, &it);
            ListView_SetItemText(list, r, 1, gPickRows[i].path);
        }
    }
    if (row > 0)
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

static LRESULT CALLBACK PickProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            MakeChild(wnd, L"STATIC", L"Filter:", SS_RIGHT | SS_CENTERIMAGE,
                      12, 15, 52, 22, 0);
            MakeChild(wnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL,
                      68, 12, 460, 26, IDC_PICKFILTER);

            HWND list = MakeChild(wnd, WC_LISTVIEWW, L"",
                LVS_REPORT | LVS_SINGLESEL | WS_BORDER, 12, 48, 516, 318, IDC_PICKLIST);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            SetWindowTheme(list, L"Explorer", NULL);
            ListView_SetBkColor(list, C_SURFACE);
            ListView_SetTextColor(list, C_TEXT);
            ListView_SetTextBkColor(list, C_SURFACE);
            LVCOLUMNW c = { 0 }; c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = L"Process"; c.cx = 165; ListView_InsertColumn(list, 0, &c);
            c.pszText = L"Path";    c.cx = 336; ListView_InsertColumn(list, 1, &c);

            FillProcessList(list);
            ApplyFilter(wnd, L"");

            MakeBtn(wnd, L"Add",    332, 378, 96, 30, IDC_PICKOK);
            MakeBtn(wnd, L"Cancel", 432, 378, 96, 30, IDC_PICKCANCEL);

            EnumChildWindows(wnd, ApplyFontEnum, (LPARAM)gUiFont);
            return 0;
        }
        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wp;
            RECT rc; GetClientRect(wnd, &rc);
            FillRect(hdc, &rc, gBrBg);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
            return HandleCtlColor(msg, wp, lp);
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
            if (di->CtlType == ODT_BUTTON) DrawThemedButton(di);
            return TRUE;
        }
        case WM_NOTIFY: {
            LPNMHDR nh = (LPNMHDR)lp;
            if (nh->idFrom == IDC_PICKLIST && nh->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)     return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    BOOL sel = (ListView_GetItemState(nh->hwndFrom,
                        (int)cd->nmcd.dwItemSpec, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                    cd->clrText   = sel ? RGB(255, 255, 255) : C_TEXT;
                    cd->clrTextBk = sel ? C_ACCENT : C_SURFACE;
                    return CDRF_DODEFAULT;
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDC_PICKFILTER && code == EN_CHANGE) {
                wchar_t filter[256];
                GetDlgItemTextW(wnd, IDC_PICKFILTER, filter, _countof(filter));
                ApplyFilter(wnd, filter);
            } else if (id == IDC_PICKOK) {
                HWND list = GetDlgItem(wnd, IDC_PICKLIST);
                int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    LVITEMW it = { 0 }; it.mask = LVIF_PARAM; it.iItem = sel;
                    ListView_GetItem(list, &it);
                    gPickResult = (int)it.lParam;
                }
                gPickDone = true; DestroyWindow(wnd);
            } else if (id == IDC_PICKCANCEL) {
                gPickResult = -1; gPickDone = true; DestroyWindow(wnd);
            }
            return 0;
        }
        case WM_CLOSE:
            gPickResult = -1; gPickDone = true; DestroyWindow(wnd);
            return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

bool PickRunningProcess(HWND parent, wchar_t* outName, int nameCch, wchar_t* outPath, int pathCch) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc   = PickProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = L"UAC_PickWnd";
        wc.hbrBackground = NULL;
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassW(&wc); reg = true;
    }
    gPickResult = -1; gPickDone = false;
    DWORD pickStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT prc = { 0, 0, 540, 420 };
    AdjustWindowRectEx(&prc, pickStyle, FALSE, 0);
    HWND wnd = CreateWindowExW(0, L"UAC_PickWnd", L"Pick a running process",
        pickStyle, CW_USEDEFAULT, CW_USEDEFAULT,
        prc.right - prc.left, prc.bottom - prc.top,
        parent, NULL, GetModuleHandleW(NULL), NULL);
    ShowWindow(wnd, SW_SHOW);
    SetFocus(GetDlgItem(wnd, IDC_PICKFILTER));
    EnableWindow(parent, FALSE);
    MSG m;
    while (!gPickDone && GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN) {
            if (m.wParam == VK_ESCAPE) {
                wchar_t filter[256];
                GetDlgItemTextW(wnd, IDC_PICKFILTER, filter, _countof(filter));
                if (filter[0])
                    SetDlgItemTextW(wnd, IDC_PICKFILTER, L"");
                else {
                    gPickResult = -1; gPickDone = true; DestroyWindow(wnd);
                }
                continue;
            }
            if (m.wParam == VK_RETURN) {
                HWND list = GetDlgItem(wnd, IDC_PICKLIST);
                int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    LVITEMW it = { 0 }; it.mask = LVIF_PARAM; it.iItem = sel;
                    ListView_GetItem(list, &it);
                    gPickResult = (int)it.lParam;
                    gPickDone = true; DestroyWindow(wnd);
                }
                continue;
            }
        }
        if (!IsDialogMessageW(wnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (gPickResult < 0) return false;
    wcscpy_s(outName, nameCch, gPickRows[gPickResult].name);
    wcscpy_s(outPath, pathCch, gPickRows[gPickResult].path);
    return true;
}
