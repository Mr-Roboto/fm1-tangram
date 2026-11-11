/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The named Performance bank uses a compact, LOSSLESS 7-byte representation
 * of each voice's 10-byte format-9 LFO/CC extension. This is necessary:
 * 10 * (1+12+382) + 8 = 3958 > PLAT_STORE_MAX (3840).
 *
 * TGPB v2 has ten entries of: occupied u8, 12 printable ASCII name bytes,
 * the first 312 bytes of a format-9 project record, plus 7*7 compact bytes.
 * 8 + 10 * (1+12+312+49) = 3748, within the existing flash allocation.
 * Project SAVE still uses full, uncompressed 382-byte project format 9.
 * Reads TGPB v1 (312-byte project format 8) without a boot-time flash write.
 * Empty entries are all zero. */
enum {
    PERFORMANCE_BANK_HEAD = 8,
    PERFORMANCE_COMPACT_RECORD_BYTES = PROJECT_RECORD8_BYTES + 7 * BL_MAX_VOICES,
    PERFORMANCE_ENTRY_BYTES = 1 + PERFORMANCE_PRESET_NAME_BYTES + PERFORMANCE_COMPACT_RECORD_BYTES,
    PERFORMANCE_BANK_BYTES = PERFORMANCE_BANK_HEAD + PERFORMANCE_PRESET_SLOTS * PERFORMANCE_ENTRY_BYTES,
    PERFORMANCE_V1_ENTRY_BYTES = 1 + PERFORMANCE_PRESET_NAME_BYTES + PROJECT_RECORD8_BYTES,
    PERFORMANCE_V1_BANK_BYTES = PERFORMANCE_BANK_HEAD + PERFORMANCE_PRESET_SLOTS * PERFORMANCE_V1_ENTRY_BYTES
};
typedef char performance_bank_must_fit[PERFORMANCE_BANK_BYTES <= PLAT_STORE_MAX ? 1 : -1];
typedef char performance_bank_is_3748[PERFORMANCE_BANK_BYTES == 3748 ? 1 : -1];
static const uint8_t performance_head[PERFORMANCE_BANK_HEAD] = {'T', 'G', 'P', 'B', 2,
    PERFORMANCE_PRESET_SLOTS, PERFORMANCE_PRESET_NAME_BYTES, 0};
static const uint8_t performance_legacy_head[PERFORMANCE_BANK_HEAD] = {'T', 'G', 'P', 'B', 1,
    PERFORMANCE_PRESET_SLOTS, PERFORMANCE_PRESET_NAME_BYTES, 0};
static uint8_t performance_bank[PERFORMANCE_BANK_BYTES] PLAT_POOL;
static int performance_bank_ready;

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
    if (bytes == PERFORMANCE_V1_BANK_BYTES &&
        !memcmp(proj_buf, performance_legacy_head, PERFORMANCE_BANK_HEAD)) {
        /* Legacy format 8 is decoded and repacked as version 2 IN RAM.
         * A user SAVE/RENAME is required before committing the new bank. */
        for (int i = 0; i < PERFORMANCE_PRESET_SLOTS; i++) {
            const uint8_t *old = proj_buf + PERFORMANCE_BANK_HEAD + i * PERFORMANCE_V1_ENTRY_BYTES;
            uint8_t *dst = performance_bank + PERFORMANCE_BANK_HEAD + i * PERFORMANCE_ENTRY_BYTES;
            if (!old[0]) {
                for (int j = 1; j < PERFORMANCE_V1_ENTRY_BYTES; j++) if (old[j]) return -1;
                continue;
            }
            if (old[0] != 1 ||
                !performance_name_valid((const char *)old + 1, PERFORMANCE_PRESET_NAME_BYTES) ||
                project_record_read(old + 1 + PERFORMANCE_PRESET_NAME_BYTES,
                                    PROJECT_RECORD8_BYTES, &decoded, NULL)) return -1;
            uint8_t full[PROJECT_RECORD_BYTES];
            if (project_record_write(&decoded, full, sizeof full) != PROJECT_RECORD_BYTES) return -1;
            dst[0] = 1;
            memcpy(dst + 1, old + 1, PERFORMANCE_PRESET_NAME_BYTES);
            performance_pack(full, dst + 1 + PERFORMANCE_PRESET_NAME_BYTES);
        }
        performance_bank_ready = 1;
        return 0;
    }
    if (bytes != PERFORMANCE_BANK_BYTES ||
        memcmp(proj_buf, performance_head, sizeof performance_head)) return -1;
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
