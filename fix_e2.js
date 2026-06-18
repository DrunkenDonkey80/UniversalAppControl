const fs = require('fs');

// ── display.c ────────────────────────────────────────────────────────
let p = 'C:/SOFT/temp/UniversalAppControl/display.c';
let c = fs.readFileSync(p, 'utf8');

c = c.replace(/gPrimaryVcpF0Vals/g, 'gPrimaryVcpE2Vals');
c = c.replace(/gPrimaryVcpF0Count/g, 'gPrimaryVcpE2Count');

c = c.replace(
`// VCP 0xF0 preset mode labels for Dell S3422DWG (verified from ddcutil community docs).
// 0x0C=ComfortView confirmed; game codes match Dell AW3425DW/S-series pattern.
const wchar_t* GetVcpF0Label(BYTE code) {
    switch (code) {
        case 0x0C: return L"ComfortView";
        case 0x0D: return L"Standard";
        case 0x0E: return L"Movie";
        case 0x0F: return L"FPS Game";
        case 0x10: return L"RTS Game";
        case 0x11: return L"RPG Game";
        case 0x13: return L"Sports";
        case 0x31: return L"Game 1";
        case 0x32: return L"Game 2";
        case 0x34: return L"Game 3";
        case 0x36: return L"Color Space";
        default: {
            static wchar_t buf[16];
            swprintf_s(buf, 16, L"Preset 0x%02X", code);
            return buf;
        }
    }
}`,
`// VCP 0xE2 picture-mode labels for Dell S3422DWG.
// 0x0E=Warm confirmed by VCP sweep; other codes from Dell S-series community docs.
const wchar_t* GetVcpE2Label(BYTE code) {
    switch (code) {
        case 0x00: return L"Standard";
        case 0x04: return L"FPS";
        case 0x0E: return L"Warm";
        case 0x12: return L"Cool";
        case 0x14: return L"Custom Color";
        case 0x1D: return L"FPS Game";
        case 0x1E: return L"RTS Game";
        case 0x1F: return L"RPG Game";
        case 0x20: return L"Movie";
        case 0x21: return L"Sports";
        case 0x22: return L"Game";
        case 0x27: return L"Night";
        case 0x2F: return L"ComfortView";
        case 0x3A: return L"sRGB";
        default: {
            static wchar_t buf[16];
            swprintf_s(buf, 16, L"Mode 0x%02X", code);
            return buf;
        }
    }
}`
);

c = c.replace(/DisplayReadCurrentVcpF0/g, 'DisplayReadCurrentPreset');

c = c.replace(
`// Read the current VCP 0xF0 value \u2014 GET only, never changes anything.
int DisplayReadCurrentPreset(void) {`,
`// Read the current VCP 0xE2 picture-mode value \u2014 GET only, never changes anything.
int DisplayReadCurrentPreset(void) {`
);

c = c.replace(
`    __try { ok = GetVCPFeatureAndVCPFeatureReply(h, 0xF0, &vcpType, &vcpCur, &vcpMax); }
    __except(EXCEPTION_EXECUTE_HANDLER) { ok = FALSE; }
    DestroyPhysicalMonitors(n, pm);
    // vcpCur=0 is a valid VCP code on some monitors \u2014 only reject when the call itself failed
    return ok ? (int)vcpCur : PRESET_UNSET;`,
`    __try { ok = GetVCPFeatureAndVCPFeatureReply(h, 0xE2, &vcpType, &vcpCur, &vcpMax); }
    __except(EXCEPTION_EXECUTE_HANDLER) { ok = FALSE; }
    DestroyPhysicalMonitors(n, pm);
    return ok ? (int)vcpCur : PRESET_UNSET;`
);

c = c.replace(
`    wcscpy_s(gMonPresets[i].name, 64, GetVcpF0Label((BYTE)vcpCode));`,
`    wcscpy_s(gMonPresets[i].name, 64, GetVcpE2Label((BYTE)vcpCode));`
);

// DisplayInit: parse E2( instead of F0(
c = c.replace(
`                // Parse VCP 0xF0 (named display presets: ComfortView, FPS, Game1...)
                {
                    int cntF0 = 0; BYTE valsF0[MAX_VCP14_VALS];
                    // Reuse capStr which is still in scope
                    const char* p = capStr;
                    // Find "F0(" in the capabilities string
                    while (*p) {
                        if (p[0]=='F' && p[1]=='0' && p[2]=='(') break;
                        p++;
                    }`,
`                // Parse VCP 0xE2 (Dell picture mode: Warm, Cool, Standard, etc.)
                {
                    int cntF0 = 0; BYTE valsF0[MAX_VCP14_VALS];
                    const char* p = capStr;
                    while (*p) {
                        if (p[0]=='E' && p[1]=='2' && p[2]=='(') break;
                        p++;
                    }`
);

c = c.replace(
`             gPrim.caps, gPrimaryVcp14Count, gPrimaryVcpF0Count`,
`             gPrim.caps, gPrimaryVcp14Count, gPrimaryVcpE2Count`
);

// DisplayCaptureCurrent: read 0xE2
c = c.replace(
`    // VCP 0xE2 = Dell picture mode (Warm=0x0E, Cool=0x12, Standard=0x00, etc.)
    if (GetVCPFeatureAndVCPFeatureReply(h, 0xE2, &vcpType, &vcpCur, &vcpMax)) {`,
`    if (GetVCPFeatureAndVCPFeatureReply(h, 0xE2, &vcpType, &vcpCur, &vcpMax)) {`
);
// In case the old text is still there:
c = c.replace(
`    // vcpCur=0 is a valid preset code (Custom Color); only skip when call fails
    if (GetVCPFeatureAndVCPFeatureReply(h, 0xF0, &vcpType, &vcpCur, &vcpMax)) {`,
`    if (GetVCPFeatureAndVCPFeatureReply(h, 0xE2, &vcpType, &vcpCur, &vcpMax)) {`
);

// DisplayApplyPreset: use 0xE2, remove 0x00 guard
c = c.replace(
`    // Profile Mode (VCP 0xF0) \u2014 applied FIRST so B/C/CT override preset defaults.
    // Code 0x00 = monitor's "Custom Color" read-state; writing 0x00 is undefined/destructive
    // on many monitors (can trigger a factory reset or input switch). Skip it on write.
    if (preset->ProfileMode != PRESET_UNSET && (BYTE)preset->ProfileMode != 0x00) {`,
`    // Profile Mode (VCP 0xE2) \u2014 Dell picture mode, applied FIRST so B/C override.
    if (preset->ProfileMode != PRESET_UNSET) {`
);
c = c.replace(
`            __try { got = GetVCPFeatureAndVCPFeatureReply(h, 0xF0, &fType, &fCur, &fMax); }`,
`            __try { got = GetVCPFeatureAndVCPFeatureReply(h, 0xE2, &fType, &fCur, &fMax); }`
);
c = c.replace(
`                    __try { setOk = SetVCPFeature(h, 0xF0, want); anySet = true; }`,
`                    __try { setOk = SetVCPFeature(h, 0xE2, want); anySet = true; }`
);
c = c.replace(
`                    CrashLog("[display] SetVCPF0 0x%02X -> %d\\n", want, setOk);`,
`                    CrashLog("[display] SetVCPE2 0x%02X -> %d\\n", want, setOk);`
);
c = c.replace(`    DWORD vcpF0Orig = 0;`, `    DWORD vcpE2Orig = 0;`);
c = c.replace(`                if (firstTouch) vcpF0Orig = fCur;`, `                if (firstTouch) vcpE2Orig = fCur;`);
c = c.replace(
`            if (vcpF0Orig)    gPrim.origVcpF0  = vcpF0Orig;`,
`            gPrim.origVcpF0 = vcpE2Orig;`
);

// DisplayRestoreAll: restore 0xE2 (0=Standard is safe)
c = c.replace(
`    // Only restore if original was a real settable preset (>0); 0 = custom/manual, not writable
    if (snap.origVcpF0 > 0)    SetVCPFeature(h, 0xF0,  snap.origVcpF0);`,
`    if (snap.hasSnapshot)      SetVCPFeature(h, 0xE2,  snap.origVcpF0);`
);

fs.writeFileSync(p, c, 'utf8');
console.log('display.c done');

// ── display.h ────────────────────────────────────────────────────────
p = 'C:/SOFT/temp/UniversalAppControl/display.h';
c = fs.readFileSync(p, 'utf8');
c = c.replace(/gPrimaryVcpF0Vals/g, 'gPrimaryVcpE2Vals');
c = c.replace(/gPrimaryVcpF0Count/g, 'gPrimaryVcpE2Count');
c = c.replace('const wchar_t* GetVcpF0Label(BYTE code);', 'const wchar_t* GetVcpE2Label(BYTE code);');
c = c.replace('int  DisplayReadCurrentVcpF0(void);', 'int  DisplayReadCurrentPreset(void);  // VCP 0xE2 picture mode');
c = c.replace('// Until scanned, names come from GetVcpF0Label(); B/C are PRESET_UNSET.',
              '// Until scanned, names come from GetVcpE2Label(); B/C are PRESET_UNSET.');
fs.writeFileSync(p, c, 'utf8');
console.log('display.h done');

// ── settings_ui.c ────────────────────────────────────────────────────
p = 'C:/SOFT/temp/UniversalAppControl/settings_ui.c';
c = fs.readFileSync(p, 'utf8');
c = c.replace(/DisplayReadCurrentVcpF0/g, 'DisplayReadCurrentPreset');
c = c.replace(/GetVcpF0Label/g, 'GetVcpE2Label');
fs.writeFileSync(p, c, 'utf8');
console.log('settings_ui.c done');

console.log('All done.');
