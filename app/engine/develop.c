/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "develop.h"

/* CPU.md: remainder lowers to quotient * divisor. Keep that 64-bit
 * product out of the source loop and its interleaved pattern loads. */
static __attribute__((noinline)) bl_tick_t develop_remainder(bl_tick_t a, bl_tick_t b) { return a % b; }

int bl_develop_voice(bl_voice_t *v, const bl_voice_t *theme,
                     const bl_compmotion_t *t, bl_tick_t tick, bl_pattern_t *scratch)
{
    if (v->pat.locked || v->config_dirty || v->pat.generator != theme->pat.generator ||
        t->reg_low < theme->config.reg_low || t->reg_high > theme->config.reg_high ||
        t->reg_low > t->reg_high) return -1;
    bl_vconfig_t cfg = v->config;
    int arp = cfg.gen.kind == BL_GEN_ARP || cfg.gen.kind == BL_GEN_MOTIF;
    int timing = t->length != cfg.length || (arp && t->rate != cfg.gen.a);
    cfg.reg_low = t->reg_low;
    cfg.reg_high = t->reg_high;
    if (timing) {
        /* Bounded source-prefix development only. Legacy/multi-bar material
         * stays on its own established timing. Rate is an actual engine mode. */
        if (!arp || !theme->pat.n || theme->config.length > BL_TICKS_PER_BAR ||
            t->length < 13u * (BL_PPQN / 4) || t->length > theme->config.length ||
            (t->rate != 240 && t->rate != 480)) return -1;
        int base_rate = theme->config.gen.a, old_rate = v->config.gen.a;
        if ((base_rate != 240 && base_rate != 480) || (old_rate != 240 && old_rate != 480)) return -1;
        cfg.length = t->length;
        cfg.gen.a = (int16_t)t->rate;
        cfg.gen.b = (int16_t)(theme->config.gen.b * t->rate / base_rate);
        if (cfg.gen.b < 1) cfg.gen.b = 1;
        if (cfg.gen.b >= cfg.gen.a) cfg.gen.b = cfg.gen.a - 1;
        cfg.phase = (uint8_t)(cfg.phase % bl_config_phase_count(&cfg));
        *scratch = v->pat;
        scratch->n = 0;
        for (int j = 0; j < theme->pat.n; j++) {
            const bl_pnote_t *original = &theme->pat.note[j];
            if (original->position < 0 || original->position >= BL_TICKS_PER_BAR ||
                develop_remainder(original->position, base_rate) ||
                (j && original->position <= theme->pat.note[j - 1].position)) return -1;
            int ordinal = (int)original->position / base_rate;
            int position = ordinal * t->rate;
            if ((uint32_t)position >= t->length) continue;
            bl_pnote_t note = *original;
            /* Keep current substitutions, velocities, accents and ratchets
             * wherever that source ordinal still exists. Missing suffixes
             * return from established A, never from a new random foundation. */
            for (int k = 0; k < v->pat.n; k++)
                if (v->pat.note[k].position == ordinal * old_rate) {
                    note = v->pat.note[k];
                    break;
                }
            note.position = position;
            note.gate = cfg.gen.b;
            scratch->note[scratch->n++] = note;
        }
        if (!scratch->n) return -1;
        /* Preserve local source position in onset units. The completion clock
         * is already at its own boundary; no global-bar alignment is added. */
        int phase = (int)bl_floor_mod(tick + v->pat.start_offset, v->pat.length);
        phase = phase * t->rate / old_rate;
        scratch->length = t->length;
        scratch->start_offset = bl_floor_mod((phase % (int)t->length) - tick, t->length);
        scratch->pitch_phase = cfg.phase;
        bl_direction_refold(scratch);
    }
    if (!bl_config_valid(&cfg)) return -1;
    if (!bl_config_diff(&cfg, &v->config)) return 0;
    if (timing) v->pat = *scratch;
    v->reg_low = cfg.reg_low;
    v->reg_high = cfg.reg_high;
    v->config = v->pending = cfg;
    return 1;
}
