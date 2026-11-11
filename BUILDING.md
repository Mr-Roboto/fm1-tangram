# Building and testing FM-Tangram

Status: 11 November 2026. The working musical baseline is
[`dev/tangram`](https://github.com/Mr-Roboto/fm1-tangram/tree/dev/tangram).
The isolated BLE MIDI implementation is
[`dev/tangram-ble-midi-spike`](https://github.com/Mr-Roboto/fm1-tangram/tree/dev/tangram-ble-midi-spike).
Do not merge experimental routing changes into the stable branch before
appropriate validation.

## Prerequisites

The project uses [fm1-base](https://github.com/charlesvestal/fm1-base) as
the `base/` git submodule, which includes Felucca BSP. Building the device
firmware requires Python 3 with Pillow, the JieLi pi32v2 Linux toolchain,
and the AC79 SDK files. On the existing WSL2 Ubuntu x86-64 environment the
toolchain runs natively. Build defaults are `~/.jieli/toolchain` and
`~/fw-AC79_AIoT_SDK`; use `JIELI_TOOLCHAIN`, `AC79_SDK`, and `PYTHON`
to override them.

The pinned `fm1-base` submodule needs the narrow flash compatibility patch
provided as `tools/fm1-base-flash-compat.patch`. `build.sh` checks/applies
that patch idempotently. **Do not reset, clean, or update the submodule just
to make `git status` look clean**: a locally modified `base` is expected
in this setup. Always review the current submodule state before a change.

New clone:

```sh
git clone --recursive https://github.com/Mr-Roboto/fm1-tangram.git
cd fm1-tangram
git switch dev/tangram-ble-midi-spike
```

Existing local BLE checkout:

```sh
cd ~/fm1-tangram
git status --short
git branch --show-current
git pull --ff-only
```

These commands only fetch changes; they do not flash the FM-1.

## Build and simulator

```sh
./build.sh                # device firmware
./build.sh --host         # desktop/WSL2 simulator
```

Outputs:

| Output | Location |
| --- | --- |
| Firmware app | `app/build/tangram.bin` |
| Installable package | `app/build/tangram.fwsc` |
| OTA loader | `app/build/loader/ota.bin` |
| Host simulator | `app/build/host/tangram_host` |

The BLE firmware target declares `ble = true` in `app/fm1.toml`.
Build checks validate image, normal RAM and pool capacity as well as
platform instruction and register restrictions. A successful build is
**not** evidence that radio pairing or clock synchronization works.

The simulator reuses the application UI, runtime and persistence code.
It simulates the BLE link and MIDI ports, not a physical radio, iPad or
wireless timing environment.

### Focused regression tests

```sh
sh tests/host/run_device_settings.sh
```

The standalone device-settings regression passed (`device-settings: ok`)
on WSL2 on 11 November 2026. It covers BLE/USB/TRS input selections,
independent MIDI/clock output masks, rejected edits while busy,
11-byte format-4 persistence, migration and retry behavior. The host
simulator build also completed on that date.

The **focused Tangram BLE and Composer simulator smoke gate** passed on
WSL2 on 11 November 2026. To repeat it:

```sh
sh tests/run_tangram_smoke.sh
```

It runs the standalone device-settings regression and the dedicated
Composer live-form engine regression (`tests/host/run_composer_form.sh`),
builds the host simulator, and executes `ble_global.bln` (GLO page and
persistence), `ble_routing.bln` (simulated USB/BLE note/clock routing) and
`composer_form_live.bln` (HOME COMPOSE K2, deferred form adoption at the
next section and cancellation). It exits nonzero on a failing check, prints
scenario logs and leaves them under `app/build/scenarios/<scenario>/run.log`.
The Global scenario also writes two PNG previews. **Do not confuse a
simulator pass with a measured physical BLE clock test.**

Expected terminal line:
`Tangram BLE smoke tests passed (host simulation only).`

**Legacy test-suite caveat:** `tests/run_tests.sh` and
`tests/host/project_test.c` still contain historical Berlinan/
Tangram project-format-6/slot-format-8 assumptions. Tangram currently
uses project format 8 and slot format 9. The old suite is **not a
reliable all-green gate** for the BLE branch until updated, and it
also treats vendor SoftFloat's fallthrough warning as an error under
modern host GCC. The new standalone device-settings test avoids these
unrelated legacy checks. Do not silence failures or rewrite golden
data merely to force the old suite to pass.

Other simulator scenario scripts in `tests/scenarios/` are inherited
from earlier development stages and have not all been revalidated
against the four-page Global UI. Refer to `base/host/plat_host.c` and
`app/host.c` for simulator command syntax.

## Hardware installation and verification

The project provides `tools/install_firmware.py`. For the tested
Windows/WSL2 workflow, after a **successful firmware build** use Windows
PowerShell with the project path set in that same PowerShell session:

```powershell
$repo = '\\wsl.localhost\Ubuntu\home\falke\fm1-tangram'
py -3.12 "$repo\tools\install_firmware.py" "$repo\app\build\tangram.fwsc" --yes
```

The native Windows Python interpreter needs the previously configured
MIDI dependencies. Choose the actual USB device/update mode carefully.
The stable branch does not change when flashing a build of the BLE
experiment; however, backing up important project and preset data
before experimental firmware installation remains prudent.

**Confirmed on FM-1 hardware (11 November 2026):** four GLO pages,
correct routing controls and Bluetooth link status; connection to an
iPad and successful BLE MIDI note playback into Korg iM1. The combined
BLE-and-Composer host smoke gate passed, and the updated firmware was built
successfully (351184-byte image; 89536/98304 bytes RAM, 335200/344064 bytes
pool). On the FM-1, Composer FORM can now change during PLAY at the next
section entry without restarting playback. A live section change does not
replace the START section chosen while stopped: STOP restores that selection.

**Additional FM-1/iPad hardware acceptance (11 November 2026,
user-confirmed):** all six agreed practical checks passed: BLE discovery/
connection, MIDI notes to Korg iM1, setting retention after reboot,
internally generated MIDI Clock over BLE, reception of an external BLE
Clock, and simultaneous USB+BLE operation with reconnection. This
completes the agreed hardware checklist, not an exhaustive validation
of every routing permutation.

**Still not measured:** quantitative BLE MIDI clock jitter/latency,
multi-hour soak stability, performance at extreme MIDI loads, and
interrupted-flash recovery. Host persistence tests and a successful
power-cycle check do not establish flash endurance.

Emergency recovery is a distinct procedure: see the pinned
`fm1-base`/BSP recovery documentation before attempting any
chip-level rescue. Normal update and rescue procedures are not
interchangeable.
