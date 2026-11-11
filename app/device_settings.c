/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included in app.c's unity build. Independent of projects, slots and undo. */
#include "app.h"
#include "device_settings.h"

/* Format 4: eleven bytes: magic "TGDS", format, sync, external clock input
 * mask, theme, lights, MIDI output mask, internal clock output mask. Import
 * format 1 (7 bytes), 2 (8), and 3 (9), preserving old fields. No padding. */
#define DEVICE_SETTINGS_MAGIC UINT32_C(0x53444754)
#define DEVICE_SETTINGS_FORMAT 4u
#define DEVICE_SETTINGS_BYTES 11u
#define DEVICE_SETTINGS_SETTLE_MS 250u

static device_settings_t device_current, device_saved;
static uint32_t device_edited_at;
static int device_dirty, device_save_error;
static int device_lights_known, device_lights_saved;
const char *const DEVICE_THEME_NAME[DEVICE_NTHEMES] = {
    "CLASSIC", "GRAPHITE", "AMBER", "NOCTURNE", "MONO", "FIRE"
};
const char *const DEVICE_MIDI_NAME[3] = {"USB", "BLE", "USB + BLE"};

static int device_settings_valid(device_settings_t settings)
{
    return settings.lights <= 2 && settings.theme < DEVICE_NTHEMES && settings.sync <= DEVICE_SYNC_EXTERNAL &&
        settings.ext_ports && !(settings.ext_ports & ~DEVICE_CLOCK_ALL) &&
        settings.midi_out && !(settings.midi_out & ~DEVICE_MIDI_BOTH) &&
        settings.clock_out && !(settings.clock_out & ~DEVICE_MIDI_BOTH);
}

static int device_settings_equal(device_settings_t a, device_settings_t b)
{
    return a.sync == b.sync && a.ext_ports == b.ext_ports && a.theme == b.theme &&
        a.lights == b.lights && a.midi_out == b.midi_out && a.clock_out == b.clock_out;
}

int device_settings_init(void)
{
    /* One extra scratch byte detects oversized records despite load's truncation. */
    uint8_t record[DEVICE_SETTINGS_BYTES + 1u];
    int n = plat_store_load(OBJ_DEVICE_SETTINGS, record, sizeof record);
    int defaults = 1;
    device_current = (device_settings_t){DEVICE_SYNC_INTERNAL, DEVICE_CLOCK_BOTH, DEVICE_THEME_CLASSIC, 1,
        DEVICE_MIDI_BOTH, DEVICE_MIDI_BOTH};
    device_lights_known = device_lights_saved = 0;
    if (n == 7 || n == 8 || n == 9 || n == (int)DEVICE_SETTINGS_BYTES) {
        uint32_t magic = (uint32_t)record[0] | (uint32_t)record[1] << 8 |
            (uint32_t)record[2] << 16 | (uint32_t)record[3] << 24;
        int supported = record[4] == (n == (int)DEVICE_SETTINGS_BYTES ? (int)DEVICE_SETTINGS_FORMAT : n - 6);
        device_settings_t loaded = {record[5], record[6], n >= 8 ? record[7] : DEVICE_THEME_CLASSIC,
            n >= 9 ? record[8] : 1,
            n == (int)DEVICE_SETTINGS_BYTES ? record[9] : DEVICE_MIDI_BOTH,
            n == (int)DEVICE_SETTINGS_BYTES ? record[10] : DEVICE_MIDI_BOTH};
        if (magic == DEVICE_SETTINGS_MAGIC && supported && device_settings_valid(loaded)) {
            device_current = loaded;
            device_lights_known = device_lights_saved = n >= 9;
            defaults = 0;
        }
    }
    device_saved = device_current;
    device_dirty = device_save_error = 0;
    device_edited_at = plat_ms();
    return defaults;                    /* defaults need no boot-time flash write */
}

device_settings_t device_settings_get(void) { return device_current; }

int device_settings_apply(void)
{
    if (rt_apply_clock_settings(device_current.sync, device_current.ext_ports)) return -1;
    return rt_apply_output_settings(device_current.midi_out, device_current.clock_out);
}

int device_settings_set(device_settings_t settings)
{
    if (!device_settings_valid(settings)) return -1;
    /* Check output routing before touching input clock settings. Both runtime
     * setters reject unsafe transport changes; only persist an accepted edit. */
    if ((settings.midi_out != device_current.midi_out || settings.clock_out != device_current.clock_out) &&
        rt_apply_output_settings(settings.midi_out, settings.clock_out)) return -1;
    /* Preserve existing apply behavior for sync/input edits; changes in
     * appearance or output routing alone do not reset the clock. */
    if (((settings.theme == device_current.theme && settings.lights == device_current.lights &&
          settings.midi_out == device_current.midi_out && settings.clock_out == device_current.clock_out) ||
         settings.sync != device_current.sync || settings.ext_ports != device_current.ext_ports) &&
        rt_apply_clock_settings(settings.sync, settings.ext_ports)) return -1;
    if (device_settings_equal(settings, device_current)) return 0;
    if (settings.lights != device_current.lights) device_lights_known = 1;
    device_current = settings;
    device_dirty = !device_lights_saved || !device_settings_equal(device_current, device_saved);
    device_save_error = 0;
    device_edited_at = plat_ms();
    return 0;
}

int device_settings_status(void)
{
    return device_save_error ? device_save_error : device_dirty ? 1 : 0;
}

void device_settings_retry(void)
{
    device_save_error = 0;
    device_edited_at = plat_ms();
}

static int device_settings_save(void)
{
    const uint8_t record[DEVICE_SETTINGS_BYTES] = {
        (uint8_t)DEVICE_SETTINGS_MAGIC, (uint8_t)(DEVICE_SETTINGS_MAGIC >> 8),
        (uint8_t)(DEVICE_SETTINGS_MAGIC >> 16), (uint8_t)(DEVICE_SETTINGS_MAGIC >> 24),
        DEVICE_SETTINGS_FORMAT, device_current.sync, device_current.ext_ports, device_current.theme, device_current.lights,
        device_current.midi_out, device_current.clock_out
    };
    int error = plat_store_save(OBJ_DEVICE_SETTINGS, record, sizeof record);
    if (error) {
        device_dirty = 1;
        device_save_error = error < 0 ? error : -1;
        plat_log("Tangram device settings: save failed");
        return device_save_error;
    }
    device_saved = device_current;
    device_lights_known = device_lights_saved = 1;
    device_dirty = 0;
    return 0;
}

int device_settings_poll(void)
{
    if (!device_dirty || device_save_error) return device_settings_status();
    if (plat_ms() - device_edited_at < DEVICE_SETTINGS_SETTLE_MS || !rt_idle()) return 1;
    return device_settings_save();
}

void device_settings_import_lights(int lights)
{
    if (device_lights_known || lights < 0 || lights > 2) return;
    device_current.lights = (uint8_t)lights;
    device_lights_known = device_dirty = 1;
    device_edited_at = plat_ms();
}

int device_settings_preserve_lights(void)
{
    if (device_lights_saved) return 0;
    if (!rt_idle() || device_save_error) return -1;
    return device_settings_save();
}
