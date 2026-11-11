/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include "plat.h"

enum { DEVICE_SYNC_INTERNAL, DEVICE_SYNC_EXTERNAL };
enum { DEVICE_THEME_CLASSIC, DEVICE_THEME_GRAPHITE, DEVICE_THEME_AMBER,
       DEVICE_THEME_NOCTURNE, DEVICE_THEME_MONO, DEVICE_THEME_FIRE, DEVICE_NTHEMES };
extern const char *const DEVICE_THEME_NAME[DEVICE_NTHEMES];
enum {
    DEVICE_CLOCK_USB = 1u << PORT_USB,
    DEVICE_CLOCK_TRS = 1u << PORT_TRS,
    DEVICE_CLOCK_BOTH = DEVICE_CLOCK_USB | DEVICE_CLOCK_TRS,
    DEVICE_CLOCK_BLE = 1u << PORT_BLE,
    DEVICE_CLOCK_ALL = DEVICE_CLOCK_USB | DEVICE_CLOCK_TRS | DEVICE_CLOCK_BLE
};
/* Deliberately identical to plat_midi_send's routing bitmask: USB=1, BLE=2, both=3. */
enum {
    DEVICE_MIDI_USB = TO_USB,
    DEVICE_MIDI_BLE = TO_BLE,
    DEVICE_MIDI_BOTH = TO_USB | TO_BLE
};
extern const char *const DEVICE_MIDI_NAME[3]; /* USB, BLE, USB + BLE */
typedef struct {
    uint8_t sync, ext_ports, theme, lights, midi_out, clock_out;
} device_settings_t;

/* Main-loop API. Clock edits apply to runtime; appearance-only edits affect drawing.
 * init resets RAM and loads the record: 0 valid, 1 missing/invalid ->
 * INT/USB+TRS/CLASSIC/LIGHTS ON, MIDI+clock on USB+BLE.
 * Format 1 imports clock choices with CLASSIC; format 2 also stores theme.
 * Format 3 adds global LIGHTS; older records import LIGHTS from the working project.
 * Format 4 adds independent MIDI and internal-clock output routing; older records
 * default to USB + BLE, as the first Tangram BLE build did.
 * set rejects invalid values or a mode change during playback/pending Start
 * (-1); an accepted edit (0) is saved after settling. Port-only edits retain
 * the existing live input-routing behavior.
 * status/poll: 0 clean, 1 pending, negative platform save error (latched).
 * poll writes only after 250 ms without edits and rt_idle(). A failed save
 * stays pending without automatic retries; retry explicitly rearms it.
 * Returning to the last saved values cancels an unnecessary write. */
int device_settings_init(void);
int device_settings_apply(void);         /* apply loaded settings after rt_init(), before project playback */
device_settings_t device_settings_get(void);
int device_settings_set(device_settings_t settings);
int device_settings_status(void);
int device_settings_poll(void);
void device_settings_retry(void);

/* Migration only: valid older working-project LIGHTS, unless already known globally.
 * Before overwriting its source with format 8, persist global LIGHTS (0 or -1).
 * May also save pending device edits; never writes a musical record. */
void device_settings_import_lights(int lights);
int device_settings_preserve_lights(void);
