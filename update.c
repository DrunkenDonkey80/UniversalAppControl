#include "update.h"
#include "Main.h"
#include "settings_ui.h"
#include <winhttp.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#pragma comment(lib, "winhttp.lib")

#define RELEASE_API  L"https://api.github.com/repos/DrunkenDonkey80/UniversalAppControl/releases/latest"
#define ASSET_PREFIX "https://github.com/DrunkenDonkey80/UniversalAppControl/releases/download/"
#define ASSET_NAME   "/UniversalAppControl.exe"
#define FIRST_CHECK_MS (60u * 1000u)            // let the network come up after login
#define CHECK_EVERY_MS (24u * 60u * 60u * 1000u)
#define MAX_DOWNLOAD   (64u << 20)

wchar_t       gUpdateVersion[32];
volatile LONG gUpdateReady;
static volatile LONG sChecking;
static HWND   sNotify;

static void SidePath(wchar_t* out, size_t cch, const wchar_t* ext) {
    swprintf_s(out, cch, L"%s%s", GetExePath(), ext);
}

// HTTPS GET into a malloc'd NUL-terminated buffer; follows redirects. Caller frees.
static char* HttpGet(const wchar_t* url, DWORD* outLen) {
    wchar_t host[256], path[2048];
    URL_COMPONENTS uc = { sizeof(uc) };
    uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = _countof(path);
    if (!WinHttpCrackUrl(url, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return NULL;

    char* buf = NULL; DWORD len = 0, cap = 0; bool ok = false;
    HINTERNET s = WinHttpOpen(APPNAME L"/" VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
          && WinHttpReceiveResponse(r, NULL)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        ok = status == 200;
        while (ok) {
            DWORD avail = 0, got = 0;
            if (!WinHttpQueryDataAvailable(r, &avail)) { ok = false; break; }
            if (!avail) break;
            if (len + avail >= MAX_DOWNLOAD) { ok = false; break; }
            if (len + avail + 1 > cap) {
                cap = (len + avail + 1) * 2;
                char* nb = realloc(buf, cap);
                if (!nb) { ok = false; break; }
                buf = nb;
            }
            if (!WinHttpReadData(r, buf + len, avail, &got)) { ok = false; break; }
            len += got;
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    if (!ok || !buf) { free(buf); return NULL; }
    buf[len] = 0;
    *outLen = len;
    return buf;
}

int UpdateCompareVersions(const char* a, const char* b) {
    int x[3] = { 0 }, y[3] = { 0 };
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;
    sscanf_s(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
    sscanf_s(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

// Copies the string value of "key" found after p; returns the position after it.
static const char* JsonStr(const char* p, const char* key, char* out, size_t cch) {
    char pat[64];
    sprintf_s(pat, sizeof(pat), "\"%s\"", key);
    if (!(p = strstr(p, pat))) return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != '"') return NULL;
    const char* e = strchr(p, '"');
    if (!e || (size_t)(e - p) >= cch) return NULL;
    memcpy(out, p, (size_t)(e - p));
    out[e - p] = 0;
    return e + 1;
}

// shortcut: substring JSON scan, enough for GitHub's flat release fields; use a real parser if fields get escaped quotes.
bool UpdateParseRelease(const char* json, char* tag, size_t tagCch, char* url, size_t urlCch) {
    if (!JsonStr(json, "tag_name", tag, tagCch)) return false;
    const char* p = json;
    size_t pn = strlen(ASSET_PREFIX), an = strlen(ASSET_NAME);
    while ((p = JsonStr(p, "browser_download_url", url, urlCch)) != NULL) {
        size_t n = strlen(url);
        // Only accept the exe from this repo's own releases.
        if (n > pn + an && strncmp(url, ASSET_PREFIX, pn) == 0 && _stricmp(url + n - an, ASSET_NAME) == 0)
            return true;
    }
    return false;
}

static bool WriteAll(const wchar_t* path, const char* data, DWORD n) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    bool ok = WriteFile(f, data, n, &w, NULL) && w == n;
    CloseHandle(f);
    if (!ok) DeleteFileW(path);
    return ok;
}

static DWORD WINAPI CheckThread(LPVOID forced) {
    int result = UPD_FAILED;
    char tag[32], url[1024], cur[32];
    sprintf_s(cur, sizeof(cur), "%ls", VERSION);
    DWORD len = 0;
    char* json = HttpGet(RELEASE_API, &len);
    if (gUpdateReady) {
        result = UPD_READY;  // never touch the downloaded file again until restart
    } else if (json && UpdateParseRelease(json, tag, sizeof(tag), url, sizeof(url))) {
        if (UpdateCompareVersions(tag, cur) <= 0) {
            result = UPD_NONE;
        } else {
            wchar_t wurl[1024], dst[MAX_PATH + 8];
            MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, _countof(wurl));
            SidePath(dst, _countof(dst), L".new");
            DWORD n = 0;
            char* exe = HttpGet(wurl, &n);
            if (exe && n > 4096 && exe[0] == 'M' && exe[1] == 'Z' && WriteAll(dst, exe, n)) {
                swprintf_s(gUpdateVersion, _countof(gUpdateVersion), L"%hs", tag);
                InterlockedExchange(&gUpdateReady, 1);
                result = UPD_READY;
            }
            free(exe);
        }
    }
    free(json);
    InterlockedExchange(&sChecking, 0);
    PostMessageW(sNotify, WM_UPDATE_DONE, (WPARAM)result, (LPARAM)forced);
    return 0;
}

void UpdateCheck(bool forced) {
    // Leftover from the previous update; may still be locked right after a restart.
    wchar_t old[MAX_PATH + 8];
    SidePath(old, _countof(old), L".old");
    DeleteFileW(old);

    if (InterlockedCompareExchange(&sChecking, 1, 0)) return;
    HANDLE t = CreateThread(NULL, 0, CheckThread, (LPVOID)(INT_PTR)forced, 0, NULL);
    if (t) CloseHandle(t);
    else InterlockedExchange(&sChecking, 0);
}

void UpdateInit(HWND notifyWnd) {
    sNotify = notifyWnd;
    SetTimer(notifyWnd, UPDATE_TIMER_ID, FIRST_CHECK_MS, NULL);
}

void UpdateOnTimer(void) {
    SetTimer(sNotify, UPDATE_TIMER_ID, CHECK_EVERY_MS, NULL);
    UpdateCheck(false);
}

// Base icon with a red "!" badge in the bottom-right corner.
static HICON MakeBadgedIcon(HICON base) {
    ICONINFO ii;
    if (!GetIconInfo(base, &ii)) return NULL;
    HICON out = NULL;
    BITMAP bm;
    if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof(bm), &bm)) {
        int w = bm.bmWidth, h = bm.bmHeight;
        BITMAPINFO bi = { 0 };
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        HDC dc = GetDC(NULL);
        DWORD* px = NULL;
        HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&px, NULL, 0);
        DWORD* mask = malloc((size_t)w * h * 4);
        BYTE* zeroMask = calloc((size_t)((w + 15) / 16) * 2 * h, 1);  // opaque; alpha decides
        if (dib && mask && zeroMask &&
            GetDIBits(dc, ii.hbmColor, 0, h, px, &bi, DIB_RGB_COLORS) &&
            GetDIBits(dc, ii.hbmMask, 0, h, mask, &bi, DIB_RGB_COLORS)) {
            bool hasAlpha = false;
            for (int i = 0; i < w * h && !hasAlpha; i++) hasAlpha = (px[i] >> 24) != 0;
            if (!hasAlpha)  // paletted icon: build alpha from the AND mask
                for (int i = 0; i < w * h; i++)
                    px[i] = (px[i] & 0xFFFFFF) | ((mask[i] & 0xFFFFFF) ? 0 : 0xFF000000u);
            float r = w * 0.3f, cx = w - r, cy = h - r;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                    if (dx * dx + dy * dy > r * r) continue;
                    bool bang = fabsf(dx) < r * 0.2f &&
                        ((dy > -r * 0.65f && dy < r * 0.2f) || (dy > r * 0.4f && dy < r * 0.7f));
                    px[y * w + x] = bang ? 0xFFFFFFFFu : 0xFFE81123u;
                }
            HBITMAP m = CreateBitmap(w, h, 1, 1, zeroMask);
            ICONINFO ni = { TRUE, 0, 0, m, dib };
            out = CreateIconIndirect(&ni);
            DeleteObject(m);
        }
        free(zeroMask);
        free(mask);
        if (dib) DeleteObject(dib);
        ReleaseDC(NULL, dc);
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    return out;
}

void UpdateOnDone(HWND wnd, int result, bool forced) {
    static bool badged = false;
    if (result == UPD_READY && !badged) {
        HICON icon = MakeBadgedIcon(gTrayNotifyIconData.hIcon);
        if (icon) gTrayNotifyIconData.hIcon = icon;
        swprintf_s(gTrayNotifyIconData.szTip, _countof(gTrayNotifyIconData.szTip),
                   APPNAME L" v" VERSION L" - update %s ready", gUpdateVersion);
        Shell_NotifyIconW(NIM_MODIFY, &gTrayNotifyIconData);
        SettingsRefreshUpdateButton();
        badged = true;
    }
    if (!forced) return;

    wchar_t msg[256];
    if (result == UPD_READY) {
        swprintf_s(msg, _countof(msg), L"Version %s is downloaded. Restart now to update?", gUpdateVersion);
        if (MessageBoxW(wnd, msg, APPNAME, MB_YESNO | MB_ICONINFORMATION) == IDYES)
            UpdateApply(wnd);
    } else if (result == UPD_NONE) {
        MessageBoxW(wnd, L"You have the latest version (v" VERSION L").", APPNAME, MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(wnd, L"Could not check for updates.\nCheck your internet connection and try again.",
                    APPNAME, MB_OK | MB_ICONWARNING);
    }
}

bool UpdateApply(HWND owner) {
    if (!gUpdateReady) return false;
    wchar_t exe[MAX_PATH], nw[MAX_PATH + 8], old[MAX_PATH + 8];
    wcscpy_s(exe, _countof(exe), GetExePath());
    SidePath(nw, _countof(nw), L".new");
    SidePath(old, _countof(old), L".old");

    // A running exe can be renamed but not overwritten.
    DeleteFileW(old);
    if (!MoveFileExW(exe, old, MOVEFILE_REPLACE_EXISTING)) goto fail;
    if (!MoveFileExW(nw, exe, 0)) { MoveFileExW(old, exe, 0); goto fail; }

    // Release the mutex so the new instance doesn't think we're still running.
    if (gMutex) { CloseHandle(gMutex); gMutex = NULL; }
    if ((INT_PTR)ShellExecuteW(NULL, L"open", exe, L"--autostart", NULL, SW_SHOWNORMAL) <= 32) {
        MoveFileExW(exe, nw, MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(old, exe, 0);
        gMutex = CreateMutexW(NULL, FALSE, APPNAME);
        goto fail;
    }
    Shell_NotifyIconW(NIM_DELETE, &gTrayNotifyIconData);
    gIsRunning = FALSE;
    PostQuitMessage(0);
    return true;

fail:
    MessageBoxW(owner, L"Could not install the update. Is the program folder writable?",
                APPNAME, MB_OK | MB_ICONERROR);
    return false;
}
