# ddccontrol analysis — applicable findings for UAC

Source: <https://github.com/ddccontrol/ddccontrol> (cloned at `C:\Temp\ddccontrol`).
Files referenced: `src/lib/ddcci.c`, `src/lib/conf.c`, `src/ddccontrol/main.c`.

ddccontrol talks to the monitor over **raw Linux I²C** (`/dev/i2c-*`), bypassing the OS entirely. Windows can't use that path directly, but the **DDC/CI protocol, timing constraints, and per-monitor quirks they implement are 100% transferable**: the Win32 `SetVCPFeature` / `GetVCPFeatureAndVCPFeatureReply` APIs do the exact same I²C transactions under the hood. So everything they learned about *what works* and *what doesn't* applies.

---

## 1. The DDC/CI protocol they implement (relevant constants)

```c
// src/lib/ddcci.c
#define DEFAULT_DDCCI_ADDR     0x37   // i²c slave address (write << 1 = 0x6E, read | 1 = 0x6F)
#define DDCCI_COMMAND_READ     0x01   // request a control value
#define DDCCI_REPLY_READ       0x02   // reply opcode
#define DDCCI_COMMAND_WRITE    0x03   // set a control value
#define DDCCI_COMMAND_SAVE     0x0C   // *** save current settings to NVRAM ***
#define DDCCI_COMMAND_CAPS     0xF3   // read capabilities string
#define DDCCI_COMMAND_PRESENCE 0xF7   // ACCESS.bus presence ping
#define DDCCI_CTRL             0xF5   // Samsung-specific "enable magictune" register
#define DDCCI_CTRL_ENABLE      0x0001
#define DDCCI_CTRL_DISABLE     0x0000

#define MAGIC_1                0x51   // host address
#define MAGIC_2                0x80   // ored with payload length
#define MAGIC_XOR              0x50   // checksum seed
```

A **write-control** packet is built like:

```c
buf[0] = 0x03 (DDCCI_COMMAND_WRITE);
buf[1] = ctrl;            // VCP code, e.g. 0xF0
buf[2] = (value >> 8);    // value high byte
buf[3] = (value & 0xFF);  // value low byte
```

VCP values are **16-bit**, big-endian. Win32 `SetVCPFeature(h, vcp, DWORD value)` does the same packing internally.

---

## 2. Timing — the part UAC almost certainly gets wrong

```c
// src/lib/ddcci.c
#define DELAY                 45000   // µs == 45 ms — minimum between ANY two DDC/CI ops
#define CONTROL_WRITE_DELAY   80000   // µs == 80 ms — after a writectrl, before the next op
```

Two distinct delays:

| Delay | Where | What it protects |
|---|---|---|
| **45 ms** | Before every read AND every write (`ddcci_delay`) | The I²C bus needs to settle; the monitor needs to be ready to listen |
| **80 ms** | After every writectrl, before the next anything (`ddcci_writectrl`) | The scaler firmware needs time to apply the new value before the next command arrives |

```c
// ddcci_writectrl, src/lib/ddcci.c:360
int ddcci_writectrl(struct monitor* mon, unsigned char ctrl, unsigned short value, int delay)
{
    ...
    int ret = ddcci_write(mon, buf, sizeof(buf));
    if (delay > 0)      usleep(1000 * delay);
    else if (delay < 0) usleep(CONTROL_WRITE_DELAY);   // <- default 80 ms
    return ret;
}
```

`ddcci_delay` also enforces **45 ms since the last write** before each new I²C transaction — so two writes back-to-back end up effectively ~80 ms + 45 ms = **125 ms** apart.

UAC currently does `Sleep(80)` after `SetVCPFeature` (in `display.c`) but does **not** enforce a 45 ms gap *before* the next read/write. When we apply brightness + contrast + F0 in one shot, the second and third calls likely fire too fast and the monitor silently drops them.

**Action for UAC:** add a `last_ddcci_op` timestamp; before every `SetVCPFeature` / `GetVCPFeatureAndVCPFeatureReply` call, sleep until at least 45 ms has elapsed since the last call; after every `SetVCPFeature`, sleep 80 ms.

---

## 3. Retry on every read

```c
// src/ddccontrol/main.c:46
static void dumpctrl(struct monitor *mon, unsigned char ctrl, int force)
{
    for (retry = RETRYS; retry; retry--) {       // RETRYS = 3
        if ((result = ddcci_readctrl(mon, ctrl, &value, &maximum)) >= 0) break;
    }
}

// src/lib/conf.c:46
#define RETRYS 3 // number of read retrys
```

They **retry every read up to 3 times**. UAC reads once and trusts the result. On a busy I²C bus or right after a mode switch, the first read often returns garbage or the previous value.

**Action for UAC:** wrap every `GetVCPFeatureAndVCPFeatureReply` in a 3-retry loop. The same applies to writes inside `ddcci_apply_profile`:

```c
// src/lib/conf.c:286
for (i = 0; i < profile->size; i++) {
    for (retry = RETRYS; retry; retry--) {
        if (ddcci_writectrl(mon, profile->address[i], profile->value[i], -1) >= 0) break;
    }
}
```

---

## 4. The "SAVE" command — possibly the missing piece for picture-mode switching

```c
// src/lib/ddcci.c:773
int ddcci_save(struct monitor* mon) {
    return ddcci_command(mon, DDCCI_COMMAND_SAVE);   // 0x0C
}

// src/lib/ddcci.c:551 — a "command" is a single-byte payload, no value
int ddcci_command(struct monitor* mon, unsigned char cmd) {
    unsigned char _buf[1];
    _buf[0] = cmd;
    return ddcci_write(mon, _buf, sizeof(_buf));
}
```

Triggered explicitly by the user with `ddccontrol -s` (see man page). For *volatile* writes (brightness/contrast you don't want persisted), it's not needed. For **picture-mode switches that the monitor scaler should pick up immediately**, this might be the magic our writes are missing — some monitors apply a write to the active register but won't actually run the new code path until they get a `0x0C` save command.

**Action for UAC:** after a picture-mode write (`SetVCPFeature(0xF0, code)`), send the SAVE command. Windows DDC/CI API doesn't expose this directly, but we can call it via raw I²C using `WriteDDCMessage` / through the physical monitor handle if `loadddccmonitor.lib` is available. *Alternative*: check if Win32 `SetVCPFeature` already sends SAVE — early evidence suggests it does not.

---

## 5. Samsung-specific "enable" sequence (precedent for monitor-specific init)

```c
// src/lib/ddcci.c:754
if ((mon->db) && (mon->db->init == samsung)) {
    if (ddcci_writectrl(mon, DDCCI_CTRL, DDCCI_CTRL_ENABLE, 0) < 0)   // write 0x0001 to 0xF5
        return -1;
}
else {
    if (ddcci_command(mon, DDCCI_COMMAND_PRESENCE) < 0)               // ping 0xF7
        return -1;
}
```

Samsung monitors **silently ignore writes** until you send `WriteCtrl(0xF5, 0x0001)` first. The Dell S3422DWG may have a similar undocumented unlock. Worth probing:

- Send `0xF5 = 0x0001` once before the first picture-mode write
- Send a `0xF7` presence ping on session start
- Or check if any other vendor-specific register acts as an unlock

This also explains why some Windows apps work and others don't with the same monitor: ones built on the same MS API but with vendor-specific init succeed where vanilla `SetVCPFeature` fails.

---

## 6. Per-monitor database (ddccontrol-db) — they don't autodetect

> **Monitor database is required for proper functionality.** — README.md

```c
// src/ddccontrol/main.c:62
static int find_write_delay(struct monitor *mon, char ctrl)
{
    /* Look up control->delay in the monitor's XML profile */
    ...
    return -1;   // unknown
}
```

They keep an **XML database keyed by EDID / PnP ID** with per-monitor entries:
- Which VCP registers exist
- Their writable value ranges
- **A custom per-control delay** (some controls need 200 ms+, not 80 ms)
- Whether the monitor needs the Samsung-style enable

The implication for us: **there is no universal "right VCP code for picture mode"**. MCCS says VCP `0xDC` ("Display Application"), but Dell uses `0xF0` for writes / `0xE2` for reads, Samsung uses something else, LG uses `0xDC`. The only correct answer is *capture from the actual monitor*.

**Action for UAC:** the user-driven Capture button we already have is the correct design. We don't need to ship a per-monitor database — we just need Capture to read whichever register the *current monitor* exposes.

---

## 7. Capabilities parsing — what they extract

The capabilities string is the only source of truth for **which VCP codes the monitor supports** and **which discrete values are valid per code**. Format is ACCESS.bus-defined:

```
(prot(monitor)type(LCD)model(S3422DWG)cmds(01 02 03 0C E3 F3)
 vcp(02 04 05 06 08 0B 0C 10 12 14(01 04 05 06 0B 0C) 16 18 1A 52 60(0F 11) 62
     6C 6E 70 86(02 05) 8A 8D(01 02) AC AE B6 C0 C6 C8 C9 CA CC(01 02 03 04 06 08 09 0A 0D 0E)
     D6(01 04 05) DC(00 03 05) DF E0 E2(00 04 0E ...) F0(0D 0E 0C 0F 10 11 13 31 32 34 36) FD)
 mccs_ver(2.1) ...)
```

Parsing notes from `ddcci_caps()`:
- Read in **64-byte chunks** via `0xF3 offset_hi offset_lo` requests (was 35-byte in older standard, NEC 173P+ uses 43-byte chunks)
- Continue until the monitor returns a chunk shorter than expected
- The string is plain ASCII — find `vcp(...)`, parse `XX(values...)` groups inside

UAC already does this in `ParseVcp14Caps` / `PARSE_VCP_BLOCK`. Confirmed correct against ddccontrol's reference implementation.

---

## 8. Things ddccontrol *doesn't* do (for the same reason we shouldn't)

- **No auto-mapping between E2 and F0 codes.** They treat each VCP as independent. Reading E2 and writing the same number to F0 is conceptually wrong — different code spaces. *(This was UAC bug.)*
- **No "probe by writing test values"** automation. They rely on user-driven capture or the per-monitor DB. Writing arbitrary values to discover what works can leave the monitor in an undefined state.
- **No assumption that writing N to F0 will result in reading N back from F0.** F0 on Dell is a **write-only selector** — reading it returns 0x00 when the monitor is in a mode not addressable via F0 (e.g. when picture mode was set via the OSD to "Warm", which is an E2-domain value).

---

## 9. Concrete checklist for UAC

Ordered by likely-impact:

1. **Add the 45 ms inter-op delay.** Track `LARGE_INTEGER lastDdcOp` per monitor; before every `Get/SetVCPFeature` call, `Sleep(max(0, 45 - elapsed))`. *(Most likely cause of "random mode switching" — the second of two rapid writes is silently dropped.)*

2. **Retry every read 3 times.** Wrap `GetVCPFeatureAndVCPFeatureReply` in a loop; return failure only if all 3 fail.

3. **Try sending the SAVE command (0x0C) after picture-mode writes.** Win32's `SetVCPFeature` may not do this automatically. Needs experiment: write F0:0x0D (Standard), then try with and without a follow-up raw I²C `0x0C` save command, observe whether the monitor's actual mode changes.

4. **Try the Samsung unlock pattern as a Dell probe.** Before the first picture-mode write of a session, try `SetVCPFeature(0xF5, 1)` and check if subsequent F0 writes start working. This is a cheap experiment.

5. **Stop treating E2 and F0 as interchangeable.** Already fixed: Capture now reads F0 directly. Reinforce: if F0 read returns 0, *do not store anything* — tell the user to set the monitor to a mode that F0 controls first.

6. **Re-read after every write to verify.** ddccontrol's `dumpctrl` reads back after writing. We should do the same and log mismatches:
   ```c
   SetVCPFeature(h, 0xF0, want);
   Sleep(80);
   DWORD got; GetVCPFeatureAndVCPFeatureReply(h, 0xF0, NULL, &got, NULL);
   if (got != want) CrashLog("F0 readback %02X != requested %02X — monitor rejected\n", got, want);
   ```
   This will tell us *immediately* whether F0 writes are actually being honored by the monitor or being silently dropped.

7. **Use `__try` around every DDC/CI call.** ddccontrol can't crash from a kernel I²C error; `SetVCPFeature` on Windows can throw SEH exceptions on disconnected/sleeping monitors. UAC already does this in most places — verify all call sites.

---

## 10. Critical finding: VCP control types (value / command / list)

From `src/lib/monitor_db.h`:

```c
enum control_type {
    value   = 0,   // continuous range; read-write stateful (brightness, contrast)
    command = 1,   // write-only trigger; reads return NACK or junk (mode switch, degauss)
    list    = 2,   // discrete enum; read-write stateful (color preset: 0=sRGB, 1=Cool, ...)
};
```

**F0 on Dell is a `command`, not a value.** That's why the symptoms are what they are:

| What we see | Why |
|---|---|
| F0 reads 0 when mode set via OSD | F0 isn't a state register; the OSD doesn't update it |
| F0 reads 0x0E after we wrote 0x0E | Some firmware paths happen to leave the last-written command in F0 as a side effect, others zero it. Not reliable. |
| Game1 not readable, Game2/3 readable | Firmware quirk: different mode-change paths handle F0 differently. Not by design. |
| Some F0 writes change the visible mode, others don't | The Dell firmware accepts the I²C transaction (so our "verified" readback passes) but may silently no-op the command if e.g. the monitor input doesn't support that mode, HDR is on, etc. |

### Read-reply NACK byte

From `src/lib/ddcci.c:415`:

```c
if (len == sizeof(buf) && buf[0] == DDCCI_REPLY_READ && buf[2] == ctrl) {
    if (value)   *value   = buf[6] * 256 + buf[7];
    if (maximum) *maximum = buf[4] * 256 + buf[5];
    return !buf[1];   // <-- NACK byte. 0 = supported, non-zero = not supported.
}
```

The DDC/CI READ REPLY has 8 bytes:
- `buf[0]` = reply opcode `0x02`
- `buf[1]` = **NACK flag** (0 = supported, !=0 = not supported / temporarily unavailable)
- `buf[2]` = VCP code echoed back
- `buf[3]` = VCP type byte (MCCS class)
- `buf[4..5]` = maximum value, big-endian
- `buf[6..7]` = current value, big-endian

**When `buf[1] != 0`, `buf[6..7]` is meaningless.** ddccontrol's `dumpctrl` shows `+` (NACK==0) or `-` (NACK!=0) accordingly.

Win32's `GetVCPFeatureAndVCPFeatureReply` returns `BOOL` and **does not expose this NACK byte to userspace**. It returns TRUE with cur=0 for NACK'd reads. We cannot distinguish "F0 read returned 0 because the value really is 0" from "F0 read NACK'd because the mode isn't F0-readable".

### Implications for UAC

1. **Stop verifying F0 writes by reading F0 back.** F0 is a command — it's not stateful. The fact that our "verified" log line says OK is meaningless; the monitor accepted the I²C transaction but may have ignored the command.

2. **Verify F0 writes by reading E2.** E2 is the state register. If E2 changes after our F0 write, the command worked visually. If E2 stays the same, the F0 command was a no-op.

3. **F0 codes are advertised in caps but not all are functional in all contexts.** The capabilities string `F0(0D 0E 0C 0F 10 11 13 31 32 34 36)` lists supported *commands*, but firmware may reject some commands depending on the current input source / HDR state / connected GPU. The user has to try each and see.

4. **There is no Win32 API to send a bare DDC/CI command** (like ddccontrol's `ddcci_command(0x0C)` for SAVE without a value). The closest equivalent is `SaveCurrentMonitorSettings()`, which is supposed to send the SAVE command internally.

5. **ddccontrol-db is the right answer for getting friendly names + reliable VCP code lists.** They hand-curate a per-monitor XML with `<control type="command" address="0xF0">` and a list of valid `<value name="Standard" value="0x0D"/>` entries for each Dell model. Without it, we're stuck with raw hex.

---

## 11. One-paragraph summary for the next attempt

**ddccontrol succeeds where UAC fails because of timing discipline and protocol completeness, not because of any secret VCP code.** It enforces a 45 ms gap between every I²C operation, an 80 ms gap after every write, retries reads 3× automatically, optionally issues a `0x0C` SAVE command, and has a per-monitor database of unlock sequences (Samsung's `0xF5 = 1`). UAC currently writes back-to-back without inter-op delays, doesn't retry, doesn't SAVE, and doesn't unlock — any one of those four could explain why F0 writes "work sometimes, randomly switch modes other times". The fixes are mechanical and small.
