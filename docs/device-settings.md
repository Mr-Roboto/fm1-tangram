# Global device settings (GLOBAL1 / GLOBAL2 / GLOBAL3)

Status: **NOT TESTED**. This stage stores INTERNAL/EXTERNAL synchronization and
USB/TRS/BOTH clock input, defaulting to INTERNAL/BOTH. These settings are now
authoritative for runtime, existing GLO controls and host clock-setting commands.
The project record is format 6, with read-only compatibility for format 5; old
project clock values are ignored. Snapshot slot format 8 is unchanged.

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

The seven-byte payload is little-endian magic `TGDS`, format byte 1, sync byte
and port-mask byte. There are no reserved fields or serialized padding.
Missing, malformed, oversized or unsupported records select defaults without
writing flash at boot. `device_settings_init()` returns 1 for that fallback,
otherwise 0, and is called during `app_init()` after `rt_init()`. The loaded
settings are applied immediately afterward, before project load/application.

`device_settings_get()` returns the current value; `device_settings_set()`
validates both fields atomically and applies accepted changes to runtime.
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
