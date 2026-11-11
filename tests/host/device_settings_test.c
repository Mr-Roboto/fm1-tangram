/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent host regression test of Tangram's device-wide BLE/USB MIDI
 * routing and persistent clock settings (no musical project or SoftFloat).
 *
 * cc -std=c99 -O2 -Wall -Wextra -Werror -DPLAT_HOST \
 *   -Iapp -Iapp/engine -Ibase/include -Itests/host \
 *   -o app/build/tests/device_settings_test tests/host/device_settings_test.c
 */
#include <stdint.h>
#include <string.h>
#include "app.h"
#include "test.h"

enum { RECORD_CAP = 32 };
static uint8_t stored[RECORD_CAP];
static int stored_len = -1, writes, logs, fail_write;
static uint32_t now_ms = 1000;
static int playing, pending_start, busy, input_sync, input_ports;
static int output_midi, output_clock;

uint32_t plat_ms(void) { return now_ms; }
void plat_log(const char *msg) { (void)msg; logs++; }
int rt_idle(void) { return !playing && !pending_start && !busy; }
int plat_store_load(uint32_t object, void *dst, uint32_t max)
{
    if (object != OBJ_DEVICE_SETTINGS || stored_len < 0) return -1;
    uint32_t n = (uint32_t)stored_len < max ? (uint32_t)stored_len : max;
    memcpy(dst, stored, n);
    return (int)n;
}
int plat_store_save(uint32_t object, const void *src, uint32_t n)
{
    if (object != OBJ_DEVICE_SETTINGS || n > RECORD_CAP) return -1;
    if (fail_write) return -9;
    memcpy(stored, src, n);
    stored_len = (int)n;
    writes++;
    return 0;
}
int rt_apply_clock_settings(int sync, int ports)
{
    if ((sync != 0 && sync != 1) || !ports || (ports & ~DEVICE_CLOCK_ALL) ||
        (sync != input_sync && (playing || pending_start))) return -1;
    input_sync = sync;
    input_ports = ports;
    return 0;
}
int rt_apply_output_settings(int midi, int clock)
{
    if (!midi || (midi & ~DEVICE_MIDI_BOTH) || !clock || (clock & ~DEVICE_MIDI_BOTH) ||
        ((midi != output_midi || clock != output_clock) &&
         (playing || pending_start || busy))) return -1;
    output_midi = midi;
    output_clock = clock;
    return 0;
}

#include "../../app/device_settings.c"

static void reset_all(void)
{
    stored_len = -1;
    writes = logs = fail_write = 0;
    now_ms = 1000;
    playing = pending_start = busy = 0;
    input_sync = DEVICE_SYNC_INTERNAL;
    input_ports = DEVICE_CLOCK_BOTH;
    output_midi = output_clock = DEVICE_MIDI_BOTH;
    CHECK(device_settings_init() == 1, "missing record uses defaults");
    CHECK(!device_settings_apply(), "default routing applies to runtime");
}

static void test_defaults_and_masks(void)
{
    reset_all();
    device_settings_t d = device_settings_get();
    CHECK(d.sync == DEVICE_SYNC_INTERNAL && d.ext_ports == DEVICE_CLOCK_BOTH &&
          d.theme == DEVICE_THEME_CLASSIC && d.lights == 1 &&
          d.midi_out == DEVICE_MIDI_BOTH && d.clock_out == DEVICE_MIDI_BOTH,
          "defaults include both USB and BLE MIDI/clock output");
    d.ext_ports = DEVICE_CLOCK_ALL;
    d.midi_out = DEVICE_MIDI_BLE;
    d.clock_out = DEVICE_MIDI_USB;
    CHECK(!device_settings_set(d) && input_ports == DEVICE_CLOCK_ALL &&
          output_midi == DEVICE_MIDI_BLE && output_clock == DEVICE_MIDI_USB,
          "combined USB/TRS/BLE clock input and independent outputs");
    CHECK(device_settings_status() == 1, "changed settings are pending");
    CHECK(device_settings_poll() == 1 && writes == 0, "settle interval is enforced");
    now_ms += 250;
    busy = 1;
    CHECK(device_settings_poll() == 1 && writes == 0, "unsent MIDI defers flash write");
    busy = 0;
    playing = 1;
    CHECK(device_settings_poll() == 1 && writes == 0, "playback defers flash write");
    playing = 0;
    CHECK(!device_settings_poll() && stored_len == 11 && writes == 1 &&
          !memcmp(stored, "TGDS", 4) && stored[4] == 4 &&
          stored[5] == DEVICE_SYNC_INTERNAL && stored[6] == DEVICE_CLOCK_ALL &&
          stored[7] == DEVICE_THEME_CLASSIC && stored[8] == 1 &&
          stored[9] == DEVICE_MIDI_BLE && stored[10] == DEVICE_MIDI_USB,
          "format 4 writes exactly eleven documented bytes");
    output_midi = output_clock = 0;
    input_ports = 0;
    CHECK(!device_settings_init() && !device_settings_apply() &&
          output_midi == DEVICE_MIDI_BLE && output_clock == DEVICE_MIDI_USB &&
          input_ports == DEVICE_CLOCK_ALL, "reboot restores both outputs and BLE input");
    d = device_settings_get();
    d.midi_out = 0;
    CHECK(device_settings_set(d) == -1, "zero MIDI output is invalid");
    d.midi_out = 4;
    CHECK(device_settings_set(d) == -1, "unsupported MIDI output bits are invalid");
    d.midi_out = DEVICE_MIDI_BLE;
    d.ext_ports = 0;
    CHECK(device_settings_set(d) == -1, "zero external clock input is invalid");
    d.ext_ports = 8;
    CHECK(device_settings_set(d) == -1, "unsupported external clock input bit is invalid");
    CHECK(output_midi == DEVICE_MIDI_BLE && output_clock == DEVICE_MIDI_USB,
          "rejected masks do not modify runtime");
}

static void test_running_safety(void)
{
    reset_all();
    device_settings_t d = device_settings_get();
    d.midi_out = DEVICE_MIDI_USB;
    playing = 1;
    CHECK(device_settings_set(d) == -1 && output_midi == DEVICE_MIDI_BOTH,
          "output route cannot change while playing");
    playing = 0;
    busy = 1;
    CHECK(device_settings_set(d) == -1 && output_midi == DEVICE_MIDI_BOTH,
          "output route waits for queued Note-Offs to drain");
    busy = 0;
    pending_start = 1;
    CHECK(device_settings_set(d) == -1 && output_midi == DEVICE_MIDI_BOTH,
          "pending external Start blocks output changes");
    pending_start = 0;
    CHECK(!device_settings_set(d) && output_midi == DEVICE_MIDI_USB,
          "new MIDI output applies once fully stopped");
    d = device_settings_get();
    d.sync = DEVICE_SYNC_EXTERNAL;
    playing = 1;
    CHECK(device_settings_set(d) == -1 && input_sync == DEVICE_SYNC_INTERNAL,
          "sync source cannot change while playing");
    d.sync = DEVICE_SYNC_INTERNAL;
    d.ext_ports = DEVICE_CLOCK_BLE;
    CHECK(!device_settings_set(d) && input_ports == DEVICE_CLOCK_BLE,
          "external input port can be changed live as before");
}

static void test_legacy_and_bad_records(void)
{
    reset_all();
    const uint8_t legacy3[] = {'T','G','D','S',3,DEVICE_SYNC_EXTERNAL,
        DEVICE_CLOCK_TRS,DEVICE_THEME_AMBER,2};
    memcpy(stored, legacy3, sizeof legacy3);
    stored_len = (int)sizeof legacy3;
    CHECK(!device_settings_init() && !device_settings_apply(), "format 3 loads");
    device_settings_t d = device_settings_get();
    CHECK(d.sync == DEVICE_SYNC_EXTERNAL && d.ext_ports == DEVICE_CLOCK_TRS &&
          d.theme == DEVICE_THEME_AMBER && d.lights == 2 &&
          d.midi_out == DEVICE_MIDI_BOTH && d.clock_out == DEVICE_MIDI_BOTH,
          "old format keeps settings and defaults new USB+BLE outputs");
    CHECK(!writes, "migrating record is read-only");
    stored[4] = 99;
    CHECK(device_settings_init() == 1, "unknown format falls back safely");
    stored[4] = 4;
    stored_len = 11;
    stored[9] = 0;
    stored[10] = DEVICE_MIDI_USB;
    CHECK(device_settings_init() == 1, "invalid stored output falls back safely");
    stored_len = 12;
    CHECK(device_settings_init() == 1, "oversized record is rejected");
}

static void test_save_retry(void)
{
    reset_all();
    device_settings_t d = device_settings_get();
    d.clock_out = DEVICE_MIDI_BLE;
    CHECK(!device_settings_set(d), "new clock route accepted");
    now_ms += 300;
    fail_write = 1;
    CHECK(device_settings_poll() == -9 && device_settings_status() == -9 &&
          writes == 0 && logs == 1, "save error reported and logged once");
    CHECK(device_settings_poll() == -9 && logs == 1,
          "failed save does not retry automatically");
    fail_write = 0;
    device_settings_retry();
    now_ms += 300;
    CHECK(!device_settings_poll() && stored_len == 11 && stored[10] == DEVICE_MIDI_BLE,
          "explicit retry persists the clock output");
    d = device_settings_get();
    d.clock_out = DEVICE_MIDI_USB;
    CHECK(!device_settings_set(d) && device_settings_status() == 1,
          "new edit schedules persistence");
    d.clock_out = DEVICE_MIDI_BLE;
    CHECK(!device_settings_set(d) && device_settings_status() == 0,
          "return to saved choice cancels unnecessary flash write");
}

int main(void)
{
    test_defaults_and_masks();
    test_running_safety();
    test_legacy_and_bad_records();
    test_save_retry();
    printf(fails ? "device-settings: %d FAILED\n" : "device-settings: ok\n", fails);
    return fails != 0;
}
