/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Tangram 1.0 musical factory starters. Included by performance_presets.c.
 *
 * These are ordinary format-9 Performance Presets, not special playback modes.
 * Incompatible development banks are discarded; format 1 is the sole bank.
 * New/empty devices get factory starters in RAM without a flash write.
 * Everything uses existing template, generator, Composer and MIDI features. */

static void factory_pattern(proj_t *p, int voice, int generator, int steps,
                            int rate, int gate, int direction, int groove, int phase)
{
    bl_vconfig_t *c = &p->voice_config[voice];
    c->gen.kind = (uint8_t)generator;
    c->gen.a = (int16_t)rate;          /* 240=1/16, 480=1/8 */
    c->gen.b = (int16_t)gate;
    c->gen.direction = (uint8_t)direction;
    c->gen.groove = (uint8_t)groove;
    c->length = (uint32_t)steps * (BL_PPQN / 4); /* independent 1/16-step loops */
    c->phase = (uint8_t)phase;
}

static int factory_preset_make(int slot)
{
    static const char names[4][PERFORMANCE_PRESET_NAME_BYTES + 1] = {
        "BERLIN SEQ", "AMBIENT DRFT", "MINIMAL PULS", "PHASE GLASS"
    };
    static const uint8_t names_len[4] = {10, 12, 12, 11};
    static const uint8_t templates[4] = {
        BL_TPL_DEMO, BL_TPL_AMBIENT, BL_TPL_FULL, BL_TPL_DEMO
    };
    static const uint8_t roots[4] = {9, 2, 4, 0}; /* A, D, E, C */
    static const uint8_t modes[4] = {
        BL_MODE_NATURAL_MINOR, BL_MODE_DORIAN, BL_MODE_NATURAL_MINOR, BL_MODE_MAJOR
    };
    static const uint8_t bpm[4] = {110, 72, 115, 112};
    static const uint8_t density[4] = {85, 62, 80, 90};
    static const uint64_t seeds[4] = {
        UINT64_C(0x54414E4742524C31), UINT64_C(0x54414E47414D4231),
        UINT64_C(0x54414E474D494E31), UINT64_C(0x54414E4750484131)
    };
    const bl_arrcfg_t *form;
    proj_t p;
    uint8_t record[PROJECT_RECORD_BYTES];
    uint8_t *entry;
    int n;

    if (slot < 1 || slot > 4) return -1;
    entry = performance_bank + PERFORMANCE_BANK_HEAD + (slot - 1) * PERFORMANCE_ENTRY_BYTES;
    n = slot - 1;

    memset(&p, 0, sizeof p);
    tangram_template(templates[n], &proj_eng); /* existing global scratch; restored by app_init/proj_apply */
    p.magic = PROJ_MAGIC;
    p.format = PROJ_FORMAT;
    p.tpl = templates[n];
    p.key_root = roots[n];
    p.key_mode = modes[n];
    p.bpm = bpm[n];
    p.seed = seeds[n];
    p.density_pct = density[n];
    p.has_density = 1;
    p.composer_start = 0; /* INTRO */

    form = slot == 1 ? &bl_arr_slow : slot == 2 ? &bl_arr_ambient : &bl_arr_classic;
    p.arr_has = 1;
    p.arr_on = slot != 4; /* PHASE GLASS stays free-running */
    p.arr_ambient = form->ambient;
    p.arr_reprise = form->reprise;
    p.arr_min = form->min_bars;
    p.arr_max = form->max_bars;
    p.composer = (bl_compcfg_t){(uint8_t)(slot != 4), 0,
                               (uint8_t)(slot == 3 ? 2 : slot == 4 ? 0 : 1), 1};
    p.ratchet_pct = slot == 3 ? 5 : 0;

    /* Derive valid original voice configurations from each built-in template,
     * keeping legacy bass, drone, pad and heartbeat voices intact unless edited. */
    proj_eng.seed = p.seed;
    proj_eng.key.root = p.key_root;
    bl_scale_preset(p.key_mode, &proj_eng.key.scale);
    if (bl_initial_state(&proj_eng, &proj_st)) return -1;
    for (int i = 0; i < proj_eng.nvoices; i++) {
        p.channel[i] = proj_eng.voice[i].channel;
        p.delay[i] = bl_delay_config((bl_delay_t){0});
        p.voice_configured[i] = 1;
        p.voice_config[i] = proj_st.v[i].config;
    }
    for (int i = proj_eng.nvoices; i < BL_MAX_VOICES; i++)
        p.delay[i] = bl_delay_config((bl_delay_t){0});

    if (slot == 1) { /* Berlin School: independent 16/15-step melodic cycles */
        factory_pattern(&p, 0, BL_GEN_ARP, 16, 240, 135,
                        BL_DIR_UP, BL_GROOVE_STRAIGHT, 0);
        factory_pattern(&p, 1, BL_GEN_MOTIF, 15, 240, 150,
                        BL_DIR_PENDULUM, BL_GROOVE_STRAIGHT, 2);
        p.voice_config[0].density_cut = 10;
        p.voice_config[1].density_cut = 22;
        p.voice_config[2].density_cut = 10;
    } else if (slot == 2) { /* sparse drone plus occasional melodic responses */
        p.voice_config[0].density_cut = 0;  /* drone anchors the sound */
        p.voice_config[1].density_cut = 65;
        factory_pattern(&p, 2, BL_GEN_MOTIF, 15, 480, 245,
                        BL_DIR_PENDULUM, BL_GROOVE_STRAIGHT, 1);
        p.voice_config[2].density_cut = 65;
        p.voice_config[3].density_cut = 10;
        p.delay[2] = bl_delay_config((bl_delay_t){1, 4, 25, 75});
    } else if (slot == 3) { /* interlocking motifs with restrained drum kit */
        factory_pattern(&p, 0, BL_GEN_MOTIF, 16, 480, 225,
                        BL_DIR_UP, BL_GROOVE_STRAIGHT, 0);
        factory_pattern(&p, 1, BL_GEN_ARP, 15, 240, 115,
                        BL_DIR_M_PENDULUM, BL_GROOVE_PULSE_8, 1);
        p.voice_config[0].density_cut = 22;
        p.voice_config[1].density_cut = 48;
        p.voice_config[2].density_cut = 15;
        p.voice_config[3].density_cut = 62;
        p.voice_config[4].density_cut = 70;
        p.voice_config[5].gen.direction = BL_DRUM_BD_SD_CH;
        p.voice_config[5].gen.groove = BL_DRUM_SPARSE;
        p.voice_config[5].density_cut = 25;
    } else { /* Glass-like additive 16/15/13 step relationship, no Composer shifts */
        factory_pattern(&p, 0, BL_GEN_ARP, 16, 240, 155,
                        BL_DIR_UP, BL_GROOVE_STRAIGHT, 0);
        factory_pattern(&p, 1, BL_GEN_MOTIF, 15, 240, 140,
                        BL_DIR_PENDULUM, BL_GROOVE_STRAIGHT, 3);
        factory_pattern(&p, 2, BL_GEN_ARP, 13, 240, 125,
                        BL_DIR_DOWN, BL_GROOVE_STRAIGHT, 1);
        p.voice_config[0].density_cut = 5;
        p.voice_config[1].density_cut = 18;
        p.voice_config[2].density_cut = 45;
    }

    for (int i = 0; i < proj_eng.nvoices; i++)
        if (!bl_config_valid(&p.voice_config[i])) return -1;
    if (project_record_write(&p, record, sizeof record) != PROJECT_RECORD_BYTES)
        return -1;

    entry[0] = 1;
    memset(entry + 1, ' ', PERFORMANCE_PRESET_NAME_BYTES);
    memcpy(entry + 1, names[n], names_len[n]);
    performance_pack(record, entry + 1 + PERFORMANCE_PRESET_NAME_BYTES);
    return 0;
}

/* Caller decides whether a one-time bank upgrade must be committed to flash.
 * Never mark a partial factory installation as a successful upgrade. */
static int factory_presets_install(void)
{
    for (int slot = 1; slot <= 4; slot++)
        if (factory_preset_make(slot)) return -1;
    return 0;
}
