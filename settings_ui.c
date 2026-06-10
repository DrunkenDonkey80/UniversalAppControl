#include "settings_ui.h"
#include "ui_ids.h"
#include "Main.h"
#include "config.h"
#include "install.h"
#include "worker.h"
#include <commctrl.h>
#include <string.h>
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")

HWND gSettingsWnd = NULL;
static int gSelected = -1;

static LRESULT CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);

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
        ListView_SetItemText(list, i, 2, L"");   // status filled in Task 16
    }
    CheckDlgButton(wnd, IDC_STARTUP, IsStartupEnabled() ? BST_CHECKED : BST_UNCHECKED);
}

static void LoadSelectionToFields(HWND wnd, int idx) {
    gSelected = idx;
    if (idx < 0 || idx >= gNumProfiles) {
        SetDlgItemTextW(wnd, IDC_NAME, L"");
        SetDlgItemTextW(wnd, IDC_PATH, L"");
        SetDlgItemTextW(wnd, IDC_HOTKEY, L"");
        CheckDlgButton(wnd, IDC_HIDE, BST_UNCHECKED);
        CheckDlgButton(wnd, IDC_MIN, BST_UNCHECKED);
        CheckDlgButton(wnd, IDC_PAUSE, BST_UNCHECKED);
        return;
    }
    PROFILE_CONFIG* p = &gProfiles[idx];
    SetDlgItemTextW(wnd, IDC_NAME, p->ProgramExeName);
    SetDlgItemTextW(wnd, IDC_PATH, p->ProgramPath);
    wchar_t hk[64]; FormatHotkey(p->HotKey, p->HotKeyModifiers, hk, _countof(hk));
    SetDlgItemTextW(wnd, IDC_HOTKEY, hk);
    CheckDlgButton(wnd, IDC_HIDE,  p->HideEnabled  ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_MIN,   p->MinimizeEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(wnd, IDC_PAUSE, p->PauseEnabled ? BST_CHECKED : BST_UNCHECKED);
}

static HWND MakeChild(HWND parent, const wchar_t* cls, const wchar_t* text,
                      DWORD style, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
}

static void CreateControls(HWND wnd) {
    HWND list = MakeChild(wnd, WC_LISTVIEWW, L"",
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER,
        10, 10, 300, 360, IDC_LIST);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
    LVCOLUMNW c = { 0 }; c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.pszText = L"Program"; c.cx = 150; ListView_InsertColumn(list, 0, &c);
    c.pszText = L"Hotkey";  c.cx = 90;  ListView_InsertColumn(list, 1, &c);
    c.pszText = L"Status";  c.cx = 55;  ListView_InsertColumn(list, 2, &c);

    int rx = 325, lblW = 60, fx = rx + lblW, fw = 240;
    MakeChild(wnd, L"STATIC", L"Name:",   SS_RIGHT, rx, 14, lblW, 20, 0);
    MakeChild(wnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, fx, 12, fw, 22, IDC_NAME);
    MakeChild(wnd, L"STATIC", L"Path:",   SS_RIGHT, rx, 44, lblW, 20, 0);
    MakeChild(wnd, L"STATIC", L"",        SS_LEFTNOWORDWRAP, fx, 44, fw, 20, IDC_PATH);
    MakeChild(wnd, L"STATIC", L"Hotkey:", SS_RIGHT, rx, 74, lblW, 20, 0);
    MakeChild(wnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, fx, 72, fw, 22, IDC_HOTKEY);

    MakeChild(wnd, L"BUTTON", L"Hide / show",        BS_AUTOCHECKBOX, fx, 104, fw, 22, IDC_HIDE);
    MakeChild(wnd, L"BUTTON", L"Minimize / restore",  BS_AUTOCHECKBOX, fx, 128, fw, 22, IDC_MIN);
    MakeChild(wnd, L"BUTTON", L"Pause / resume",      BS_AUTOCHECKBOX, fx, 152, fw, 22, IDC_PAUSE);

    MakeChild(wnd, L"STATIC", L"", SS_LEFTNOWORDWRAP, fx, 182, fw, 40, IDC_STATUS);

    MakeChild(wnd, L"BUTTON", L"+ Add from running...", BS_PUSHBUTTON,  10,  380, 150, 26, IDC_ADD);
    MakeChild(wnd, L"BUTTON", L"Remove",                 BS_PUSHBUTTON, 168,  380,  80, 26, IDC_REMOVE);
    MakeChild(wnd, L"BUTTON", L"Run at startup",         BS_AUTOCHECKBOX, 325, 384, 140, 22, IDC_STARTUP);
    MakeChild(wnd, L"BUTTON", L"Open INI folder",        BS_PUSHBUTTON, 470,  380, 110, 26, IDC_OPENINI);
    MakeChild(wnd, L"BUTTON", L"Install to user programs...", BS_PUSHBUTTON, 325, 412, 255, 26, IDC_INSTALL);
}

void ShowSettingsWindow(HINSTANCE inst, HWND owner) {
    if (gSettingsWnd) { SetForegroundWindow(gSettingsWnd); return; }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"UAC_SettingsWnd";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }

    gSettingsWnd = CreateWindowExW(0, L"UAC_SettingsWnd", APPNAME L" - Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 615, 470, owner, NULL, inst, NULL);
    ShowWindow(gSettingsWnd, SW_SHOW);
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

    SaveConfig();
    Job j = { JOB_RELOAD_CONFIG, 0 };
    JobQueuePush(j);
    RefreshList(wnd);
}

static LRESULT CALLBACK SettingsProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            CreateControls(wnd);
            RefreshList(wnd);
            return 0;
        case WM_NOTIFY: {
            LPNMHDR nh = (LPNMHDR)lp;
            if (nh->idFrom == IDC_LIST && nh->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW nv = (LPNMLISTVIEW)lp;
                if ((nv->uChanged & LVIF_STATE) && (nv->uNewState & LVIS_SELECTED))
                    LoadSelectionToFields(wnd, (int)nv->lParam);
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
                        Job j = { JOB_RELOAD_CONFIG, 0 }; JobQueuePush(j);
                        RefreshList(wnd);
                        LoadSelectionToFields(wnd, -1);
                    }
                    break;
                case IDC_STARTUP:
                    if (code == BN_CLICKED)
                        SetStartupEnabled(IsDlgButtonChecked(wnd, IDC_STARTUP) == BST_CHECKED);
                    break;
                case IDC_OPENINI:  OpenConfigFolder(); break;
                case IDC_INSTALL:  InstallToUserPrograms(wnd); break;
                case IDC_ADD:      /* Task 17 */ break;
            }
            return 0;
        }
        case WM_CLOSE:   DestroyWindow(wnd); return 0;
        case WM_DESTROY: gSettingsWnd = NULL; return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

bool PickRunningProcess(HWND parent, wchar_t* outName, int nameCch, wchar_t* outPath, int pathCch) {
    // Implemented in Task 17
    (void)parent; (void)outName; (void)nameCch; (void)outPath; (void)pathCch;
    return false;
}
