# Global device settings — BLE routing extension (11 November 2026)

**Status (11 November 2026):** the device firmware and host simulator compiled
successfully; `sh tests/host/run_device_settings.sh` returned
`device-settings: ok`. On an FM-1, the user confirmed all four GLO pages,
visible settings, Bluetooth pairing/connection status and wireless MIDI note
playback into Korg iM1 on an iPad. This validates a real BLE-MIDI **output**
path. The new external BLE-clock **input**, separate real-time clock **output**,
multi-destination combinations, and persistence after power cycling have
**not** been independently exercised on hardware.

The `./build.sh --host` build compiled; an additional regression scenario
`tests/scenarios/ble_global.bln` exercises the displayed GLO MIDI page and
independent persisted routes in the simulator, but its execution has not yet
been confirmed. The scenario is a simulated integration test, not a
substitute for radio/clock timing measurements.

The GLO button cycles **SETUP → MIDI → APPEARANCE → PERFORMANCE**.
SETUP keeps K1 Root, K2 INT/EXT sync, K3 external clock input, K4 Lights.
K3 supports USB, TRS, BLE, USB+TRS, USB+BLE, TRS+BLE and USB+TRS+BLE.
GLO MIDI controls two **independent** output masks:

- K1 **CLOCK OUT** (USB / BLE / USB+BLE): internally generated MIDI realtime
  Start (FA), Clock (F8), Continue (FB), Stop (FC).
- K2 **MIDI OUT** (USB / BLE / USB+BLE): note-on/off and CC output, including
  generated delay echoes and note flushes.

All modes persist device-wide, independently of projects and snapshots.
Output routing changes are refused while running, a Start is pending, or
outgoing MIDI still needs draining. Input clock port changes keep their
existing live-apply behavior. The radio is a **BLE MIDI peripheral** for
iPad/computer; Bluetooth central/controller discovery is not built in.

`OBJ_DEVICE_SETTINGS` remains object ID 8, in unchanged A/B flash sectors
0xCE000–0xCFFFF. On-disk format **4** stores exactly 11 bytes:
`TGDS` (4), version (1), sync (1), external-input mask (1), theme (1),
lights (1), MIDI output mask (1), clock output mask (1).
The formats 1 (7 bytes), 2 (8 bytes) and 3 (9 bytes) still load. Migrating
them preserves the legacy settings and defaults the **new** MIDI and clock
output masks to USB+BLE, matching Tangram's earlier experimental BLE build.
No write occurs solely because of reading an older record.
Flash writes still coalesce after 250 ms and occur only when MIDI is idle.

Host debug commands: `rt port USB|TRS|BLE|both|usbble|trsble|all`,
`rt midiout USB|BLE|both`, `rt clockout USB|BLE|both`.
`expect ext_ports`, `expect midi_out`, `expect clock_out` expose these
separate choices. The output route's bit values match `TO_USB=1`,
`TO_BLE=2`, `USB+BLE=3`; external input additionally supports TRS (bit 4).

## Previous implementation notes (before BLE routing)

The following documents the original global settings design. Its old page
count, format version, USB/TRS-only routing and test-status claims are
historical and superseded by the section above.

# Global device settings (GLOBAL1 / GLOBAL2 / GLOBAL3)

Status: **NOT TESTED**. This stage stores INTERNAL/EXTERNAL synchronization and
USB/TRS/BOTH clock input and UI theme, defaulting to INTERNAL/BOTH/CLASSIC. These settings are now
authoritative for runtime, existing GLO controls and host clock-setting commands.
The musical project record is format 7, with readers for formats 5/6; old
project clock values are ignored. Musical snapshot slots use format 9 with a
format-8 reader. Global theme storage changes neither musical format.

`OBJ_DEVICE_SETTINGS` is appended as ID 8; IDs 0..7 and their locations remain
unchanged. Its A/B sectors are `[0xCE000,0xCF000)` and `[0xCF000,0xD0000)`.
Static allocation review against the pinned platform establishes:

- Existing Tangram objects occupy `[0xD0000,0xE0000)`, including panel calibration.
  No application raw-data allocation or other mapped object uses the new pair.
- `base/bsp/chip/fm1_flash.h` permits storage in `[0x97000,0xE0000)`; OTA staging,
  autosave and Felucca globals begin at 0xE0000, 0xE5000 and 0xFC000 respectively.
- `base/bsp/loader/ldr_core.c` writes firmware below 0x93000 and explicitly skips
  user-data sectors during update-record cleanup. `loader.c` excludes the whole
  user-data range from its write window. The package's flash image is 0x93000
  bytes; its VM descriptor is not a settings payload written over this pair.

The eight-byte payload is little-endian magic `TGDS`, format byte 2, sync byte,
port-mask byte and theme byte (0..5). IDs 0..4 retain their meanings; FIRE is 5.
The seven-byte format-1 reader preserves
its clock choices and defaults theme to CLASSIC. There are no reserved fields
or serialized padding; loading older settings does not write flash.
Missing, malformed, oversized or unsupported records select defaults without
writing flash at boot. `device_settings_init()` returns 1 for that fallback,
otherwise 0, and is called during `app_init()` after `rt_init()`. The loaded
settings are applied immediately afterward, before project load/application.

`device_settings_get()` returns the current value; `device_settings_set()`
validates all three fields atomically. Clock changes apply to runtime; a
theme-only edit bypasses clock application and updates display tokens directly.
Mode changes are rejected during playback or a pending external Start; routing
edits retain the existing live behavior. Unchanged edits do nothing; edits returning
to the last saved value cancel pending persistence. `device_settings_poll()`
is called from the app's main-loop UI frame/input hooks, coalesces edits for
250 ms, and writes only when `rt_idle()` reports stopped playback and drained
MIDI releases. No ISR writes or project SAVE dependency are introduced.

Persistence uses the platform's CRC-checked, readback-verified, torn-write-safe
A/B object API. On save failure, the edited RAM value remains pending; a
negative status is latched and logged once, preventing repeated flash attempts.
`device_settings_retry()` explicitly rearms persistence; a different accepted
edit also rearms it. `device_settings_status()` exposes clean (0), pending (1)
or a negative save error. Flash failures cannot guarantee the last edit survives
a restart. Edits made shortly before power loss may not yet have been saved.
GLO APPEARANCE K1 selects theme and K2 retries a failed global save. Live edits
remain pending until transport is stopped and releases are drained. Older
firmware without a format-2 reader falls back to its defaults on this record;
no downgrade writer is provided.

Host storage survives simulated `reboot` within a process, not separate simulator
processes, and does not emulate physical torn writes. The pinned update path
preserves this region; full-chip erase or another firmware's data allocation may
not. No general flash wear levelling is added, and no hardware verification has
been performed.

Project load, SAVE, STOP, UNDO and reprise never apply global clock settings.
INTERNAL uses the musical project's BPM. EXTERNAL retains its measured BPM and
rate through project loads and stopped configuration reconciliation. On boot,
before an external estimate exists, the existing runtime initialization tempo
(96 BPM) is the fallback; switching from INTERNAL to EXTERNAL initially retains
the current rate until external pulses establish an estimate. No measured tempo
is persisted. Existing FA/FB/FC interpretation and freewheel/interpolation remain.

The old `proj_set_sync`/`proj_set_ext_ports` and runtime setter names remain as
compatibility control entry points into this global API. No commands or pages
were added. Screen-reader vocabulary enumeration uses a local display preview,
never persistent setters.

## Existing GLO controls and host integration

GLO keeps its ROOT / SYNC / EXT INPUT / LIGHTS page, navigation and HELP. SYNC
reads the authoritative global source (INT/EXT); an edit while playing is
refused with the existing WHEN STOPPED message. A pending MIDI Start also
prevents a mode change. EXT INPUT reads/edits USB, TRS or BOTH (displayed as
USB + TRS); port-only edits retain their existing live behavior. Handlers call
`device_settings_set()` and never write flash themselves. ROOT and LIGHTS keep
their existing project behavior. No pages, knobs or shortcuts were added.

Existing host syntax remains:

- `rt sync int|ext`: global, stopped-only; refused edits are reported rather
  than silently appearing successful.
- `rt port USB|TRS|both`: global routing through the same device-settings API.
- `proj sync int|ext`: compatibility alias for global sync, despite its prefix.
- `expect sync` and `expect ext_ports`: read actual global values. Port values
  remain the existing `1 << PORT_USB` / `1 << PORT_TRS` bit masks.
- `proj tempo BPM`: the stored musical project's internal tempo. In EXTERNAL
  mode this does not overwrite the measured runtime BPM/rate. `rt tempo` remains
  a runtime control and cannot retime the followed external clock.

Project loading and SAVE do not change the clock record. A simulated `reboot`
calls the normal `app_init()`: RAM settings are reset, the record is loaded and
applied, and the musical project is then loaded independently. It does not flush
unsaved edits merely because a reboot was requested. To observe persistence,
allow at least 250 ms after the final edit while stopped and with MIDI releases
drained (for example, a host `wait 300` after the transport is idle). Edits made
while playing remain pending until those conditions are met; power loss or
reboot before persistence loses them. Runtime external tempo is not retained
across reboot and returns to the documented startup fallback until measured.

The screen-reader audit retains its local clock preview and restores the prior
display source afterward. It never calls the settings API to enumerate values.
Existing simulator scenarios/goldens were not updated or executed. Direct
clock integration and persistence behavior remain **NOT TESTED**.

See [project-format.md](project-format.md) for the explicit musical record and
the historical format-5 read layout. Unchanged implicit voice defaults can be
promoted for persistence without marking the transport stale. Actual changes
still retain existing reapplication and per-voice local-boundary behavior.
