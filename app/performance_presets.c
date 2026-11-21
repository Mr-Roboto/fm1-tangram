/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Tangram 1.0 Performance Preset bank, format 1.
 * Ten named entries; 3748 bytes total, stored in the existing flash object.
 * Each compact record preserves the entire format-9 project losslessly.
 * Development-format banks are discarded, never migrated. */
enum {
    PERFORMANCE_BANK_HEAD = 8,
    PERFORMANCE_COMPACT_RECORD_BYTES = PROJECT_RECORD8_BYTES + 7 * BL_MAX_VOICES,
    PERFORMANCE_ENTRY_BYTES = 1 + PERFORMANCE_PRESET_NAME_BYTES + PERFORMANCE_COMPACT_RECORD_BYTES,
    PERFORMANCE_BANK_BYTES = PERFORMANCE_BANK_HEAD + PERFORMANCE_PRESET_SLOTS * PERFORMANCE_ENTRY_BYTES
};
typedef char performance_bank_must_fit[PERFORMANCE_BANK_BYTES <= PLAT_STORE_MAX ? 1 : -1];
typedef char performance_bank_is_3748[PERFORMANCE_BANK_BYTES == 3748 ? 1 : -1];
static const uint8_t performance_head[PERFORMANCE_BANK_HEAD] = {'T', 'G', 'P', 'B', 1,
    PERFORMANCE_PRESET_SLOTS, PERFORMANCE_PRESET_NAME_BYTES, 0};
static uint8_t performance_bank[PERFORMANCE_BANK_BYTES] PLAT_POOL;
static int performance_bank_ready;
/* Byte 7 selects the last saved slot; zero means slot 01. */
int performance_preset_last_slot(void)
{
    int slot = performance_bank_ready ? performance_bank[7] : 0;
    return slot >= 1 && slot <= PERFORMANCE_PRESET_SLOTS ? slot : 1;
}

/* Pack 10 exact source bytes into 53 bits, split over seven bytes:
 * byte 0: cycle(3), target(3), override(1), reserved(1);
 * byte 1: depth(7), mixer mask bit0(1);
 * byte 2: center(7), mixer mask bit1(1);
 * byte 3..6: mixer values (4 x 7) and mask bits2..3.
 * All valid values round-trip exactly. The three unused high bits are zero. */
static void performance_pack(const uint8_t *src, uint8_t *dst)
{
    memcpy(dst, src, PROJECT_RECORD8_BYTES);
    for (int i = 0; i < BL_MAX_VOICES; i++) {
        const uint8_t *v = src + PROJECT_RECORD8_BYTES + 10 * i;
        uint8_t *x = dst + PROJECT_RECORD8_BYTES + 7 * i;
        x[0] = (uint8_t)((v[0] & 1u) << 6 | (v[1] & 7u) << 3 | (v[2] & 7u));
        x[1] = (uint8_t)((v[3] & 127u) | ((v[5] & 1u) << 7));
        x[2] = (uint8_t)((v[4] & 127u) | ((v[5] & 2u) << 6));
        uint32_t packed = (uint32_t)v[6] | (uint32_t)v[7] << 7 |
                          (uint32_t)v[8] << 14 | (uint32_t)v[9] << 21 |
                          (uint32_t)(v[5] & 12u) << 26;
        for (int j = 0; j < 4; j++) x[3 + j] = (uint8_t)(packed >> (8 * j));
    }
}

static void performance_unpack(const uint8_t *src, uint8_t *dst)
{
    memcpy(dst, src, PROJECT_RECORD8_BYTES);
    for (int i = 0; i < BL_MAX_VOICES; i++) {
        const uint8_t *x = src + PROJECT_RECORD8_BYTES + 7 * i;
        uint8_t *v = dst + PROJECT_RECORD8_BYTES + 10 * i;
        uint32_t packed = (uint32_t)x[3] | (uint32_t)x[4] << 8 |
                          (uint32_t)x[5] << 16 | (uint32_t)x[6] << 24;
        v[0] = (uint8_t)((x[0] >> 6) & 1u);
        v[1] = (uint8_t)((x[0] >> 3) & 7u);
        v[2] = (uint8_t)(x[0] & 7u);
        v[3] = (uint8_t)(x[1] & 127u);
        v[4] = (uint8_t)(x[2] & 127u);
        v[5] = (uint8_t)((x[1] >> 7) | ((x[2] >> 6) & 2u) |
                         ((packed >> 26) & 12u));
        for (int j = 0; j < MIDI_MIX_N; j++)
            v[6 + j] = (uint8_t)((packed >> (7 * j)) & 127u);
    }
}

static int performance_offset(int slot)
{
    if (!performance_bank_ready || slot < 1 || slot > PERFORMANCE_PRESET_SLOTS) return -1;
    return PERFORMANCE_BANK_HEAD + (slot - 1) * PERFORMANCE_ENTRY_BYTES;
}

static int performance_name_valid(const char *name, int length)
{
    if (!name || length < 0 || length > PERFORMANCE_PRESET_NAME_BYTES) return 0;
    for (int i = 0; i < length; i++)
        if ((uint8_t)name[i] < 32 || (uint8_t)name[i] > 126) return 0;
    return 1;
}

static int performance_project_read(const uint8_t *compact, proj_t *out)
{
    uint8_t full[PROJECT_RECORD_BYTES];
    performance_unpack(compact, full);
    if (full[4] != PROJ_FORMAT || full[5] || full[6] || full[7]) return -1;
    return project_record_read(full, sizeof full, out, NULL);
}

#include "factory_presets.c"

/* Discard an incompatible development bank. No legacy decoder, migration,
 * or version-specific compatibility path is needed before release 1.0. */
static int performance_factory_reset(int persist)
{
    memset(performance_bank, 0, sizeof performance_bank);
    memcpy(performance_bank, performance_head, sizeof performance_head);
    if (factory_presets_install()) return -1;
    if (persist && plat_store_save(OBJ_PERFORMANCE_PRESETS, performance_bank,
                                  PERFORMANCE_BANK_BYTES)) return -1;
    performance_bank_ready = 1;
    return 1;
}

int performance_presets_init(void)
{
    proj_t decoded;
    performance_bank_ready = 0;
    int bytes = plat_store_load(OBJ_PERFORMANCE_PRESETS, proj_buf, sizeof proj_buf);
    if (bytes < 0) return performance_factory_reset(0);
    if (bytes != PERFORMANCE_BANK_BYTES ||
        memcmp(proj_buf, performance_head, PERFORMANCE_BANK_HEAD - 1) ||
        proj_buf[7] > PERFORMANCE_PRESET_SLOTS)
        return performance_factory_reset(1);
    for (int i = 0; i < PERFORMANCE_PRESET_SLOTS; i++) {
        const uint8_t *entry = proj_buf + PERFORMANCE_BANK_HEAD + i * PERFORMANCE_ENTRY_BYTES;
        if (!entry[0]) {
            for (int j = 1; j < PERFORMANCE_ENTRY_BYTES; j++)
                if (entry[j]) return performance_factory_reset(1);
        } else if (entry[0] != 1 ||
            !performance_name_valid((const char *)entry + 1, PERFORMANCE_PRESET_NAME_BYTES) ||
            performance_project_read(entry + 1 + PERFORMANCE_PRESET_NAME_BYTES, &decoded))
            return performance_factory_reset(1);
    }
    memcpy(performance_bank, proj_buf, sizeof performance_bank);
    performance_bank_ready = 1;
    return 0;
}

int performance_preset_used(int slot)
{
    int at = performance_offset(slot);
    return at < 0 ? -1 : performance_bank[at];
}

int performance_preset_name(int slot, char out[PERFORMANCE_PRESET_NAME_BYTES + 1])
{
    int at = performance_offset(slot);
    if (!out || at < 0 || !performance_bank[at]) return -1;
    memcpy(out, performance_bank + at + 1, PERFORMANCE_PRESET_NAME_BYTES);
    out[PERFORMANCE_PRESET_NAME_BYTES] = 0;
    return 0;
}

int performance_preset_load(int slot)
{
    proj_t decoded;
    int at = performance_offset(slot);
    if (at < 0 || !performance_bank[at] || rt_playing() || !rt_idle() || proj_saving) return -1;
    if (performance_project_read(performance_bank + at + 1 + PERFORMANCE_PRESET_NAME_BYTES, &decoded)) return -1;
    proj = decoded;
    proj_unseeded = 0;
    proj_apply();
    return 0;
}

static int performance_commit(void)
{
    /* CRC-protected A/B flash commit; keep RAM unchanged if it fails. */
    if (plat_store_save(OBJ_PERFORMANCE_PRESETS, proj_buf, PERFORMANCE_BANK_BYTES)) return -1;
    memcpy(performance_bank, proj_buf, sizeof performance_bank);
    return 0;
}

static void performance_write_name(int at, const char *name, int length)
{
    memset(proj_buf + at + 1, ' ', PERFORMANCE_PRESET_NAME_BYTES);
    memcpy(proj_buf + at + 1, name, (size_t)length);
}

int performance_preset_store(int slot, const char *name, int length)
{
    int at = performance_offset(slot);
    if (at < 0 || !performance_name_valid(name, length) || rt_playing() || !rt_idle() || proj_saving) return -1;
    sync_voice_configs();
    draw_seed();
    uint8_t full[PROJECT_RECORD_BYTES];
    if (project_record_write(&proj, full, sizeof full) != PROJECT_RECORD_BYTES) return -1;
    memcpy(proj_buf, performance_bank, sizeof performance_bank);
    performance_pack(full, proj_buf + at + 1 + PERFORMANCE_PRESET_NAME_BYTES);
    proj_buf[at] = 1;
    performance_write_name(at, name, length);
    proj_buf[7] = (uint8_t)slot;  /* last successful STORE if commit succeeds */
    return performance_commit();
}

int performance_preset_rename(int slot, const char *name, int length)
{
    int at = performance_offset(slot);
    if (at < 0 || !performance_bank[at] || !performance_name_valid(name, length) || !rt_idle()) return -1;
    memcpy(proj_buf, performance_bank, sizeof performance_bank);
    performance_write_name(at, name, length);
    return performance_commit();
}
