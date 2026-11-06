/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included by project.c: reuse its explicit musical serializer and scratch buffer.
 * Format 1: "TGPB", version, slot count, name width, reserved zero; then ten
 * entries: occupied u8, twelve printable ASCII bytes, 312-byte format-8 project.
 * Empty entries are entirely zero. No native structs or runtime state on disk. */
enum {
    PERFORMANCE_BANK_HEAD = 8,
    PERFORMANCE_ENTRY_BYTES = 1 + PERFORMANCE_PRESET_NAME_BYTES + PROJECT_RECORD_BYTES,
    PERFORMANCE_BANK_BYTES = PERFORMANCE_BANK_HEAD + PERFORMANCE_PRESET_SLOTS * PERFORMANCE_ENTRY_BYTES
};
typedef char performance_bank_must_fit[PERFORMANCE_BANK_BYTES <= PLAT_STORE_MAX ? 1 : -1];
static const uint8_t performance_head[PERFORMANCE_BANK_HEAD] = {'T', 'G', 'P', 'B', 1,
    PERFORMANCE_PRESET_SLOTS, PERFORMANCE_PRESET_NAME_BYTES, 0};
static uint8_t performance_bank[PERFORMANCE_BANK_BYTES] PLAT_POOL;
static int performance_bank_ready;

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

static int performance_project_read(const uint8_t *record, proj_t *out)
{
    /* Bank format 1 contains only format 8, never legacy global LIGHTS. */
    if (record[4] != PROJ_FORMAT || record[5] || record[6] || record[7]) return -1;
    return project_record_read(record, PROJECT_RECORD_BYTES, out, NULL);
}

int performance_presets_init(void)
{
    proj_t decoded;
    performance_bank_ready = 0;
    memset(performance_bank, 0, sizeof performance_bank);
    memcpy(performance_bank, performance_head, sizeof performance_head);
    /* Full scratch capacity also detects oversized/truncated bank records. */
    int bytes = plat_store_load(OBJ_PERFORMANCE_PRESETS, proj_buf, sizeof proj_buf);
    if (bytes < 0) {
        performance_bank_ready = 1;
        return 1;
    }
    if (bytes != PERFORMANCE_BANK_BYTES || memcmp(proj_buf, performance_head, sizeof performance_head)) return -1;
    for (int i = 0; i < PERFORMANCE_PRESET_SLOTS; i++) {
        const uint8_t *entry = proj_buf + PERFORMANCE_BANK_HEAD + i * PERFORMANCE_ENTRY_BYTES;
        if (!entry[0]) {
            for (int j = 1; j < PERFORMANCE_ENTRY_BYTES; j++)
                if (entry[j]) return -1;
        } else if (entry[0] != 1 ||
            !performance_name_valid((const char *)entry + 1, PERFORMANCE_PRESET_NAME_BYTES) ||
            performance_project_read(entry + 1 + PERFORMANCE_PRESET_NAME_BYTES, &decoded)) return -1;
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
    /* The platform verifies its CRC-protected A/B commit. Keep RAM unchanged on
     * failure; only this object is written, never the working project or slots. */
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
    memcpy(proj_buf, performance_bank, sizeof performance_bank);
    if (project_record_write(&proj, proj_buf + at + 1 + PERFORMANCE_PRESET_NAME_BYTES, PROJECT_RECORD_BYTES) < 0) return -1;
    proj_buf[at] = 1;
    performance_write_name(at, name, length);
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
