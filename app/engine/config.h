/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
#pragma once
#include "gen.h"

/* Musical settings only: delay/channel are separate.
 * length is ticks so unedited multi-bar legacy templates retain their duration.
 * The LOOP setter below exposes 1..16 sixteenth-note steps. */
typedef struct {
    bl_key_t key;
    bl_gencfg_t gen;
    uint32_t length;
    uint8_t reg_low, reg_high;
    uint8_t phase;               /* pitch traversal positions, independent of duration */
    uint8_t density_cut;         /* 0 = full, 100 = silent; zero-initialized frames stay compatible */
} bl_vconfig_t;

enum {
    BL_CFG_GENERATOR, BL_CFG_ROOT, BL_CFG_SCALE, BL_CFG_REG_LOW, BL_CFG_REG_HIGH,
    BL_CFG_RATE, BL_CFG_GATE, BL_CFG_LOOP, BL_CFG_DIRECTION, BL_CFG_GROOVE, BL_CFG_PHASE, BL_CFG_DENSITY, BL_NCFG
};
#define BL_CFG_ALL ((1u << BL_NCFG) - 1u)

/* Straight-grid source size for an ARP/MOTIF target, before playback masks.
 * Legacy generators have no ordered traversal and expose only PHASE 0. */
static inline int bl_config_phase_count(const bl_vconfig_t *c)
{
    if (c->gen.kind != BL_GEN_ARP && c->gen.kind != BL_GEN_MOTIF) return 1;
    uint32_t spacing = c->gen.a == 240 ? 240u : 480u;
    uint32_t n = c->length ? (c->length - 1u) / spacing + 1u : 1u;
    if (n > BL_MAX_NOTES) n = BL_MAX_NOTES;
    if (c->gen.kind == BL_GEN_ARP && n > 8) n = 8;
    return (int)n;
}

static inline uint16_t bl_config_diff(const bl_vconfig_t *a, const bl_vconfig_t *b)
{
    uint16_t mask = 0;
    if (a->gen.kind != b->gen.kind) mask |= 1u << BL_CFG_GENERATOR;
    if (a->key.root != b->key.root) mask |= 1u << BL_CFG_ROOT;
    if (a->key.scale.mode != b->key.scale.mode || a->key.scale.count != b->key.scale.count)
        mask |= 1u << BL_CFG_SCALE;
    for (int i = 0; i < 12; i++) if (a->key.scale.off[i] != b->key.scale.off[i]) mask |= 1u << BL_CFG_SCALE;
    if (a->reg_low != b->reg_low) mask |= 1u << BL_CFG_REG_LOW;
    if (a->reg_high != b->reg_high) mask |= 1u << BL_CFG_REG_HIGH;
    if (a->gen.a != b->gen.a) mask |= 1u << BL_CFG_RATE;
    if (a->gen.b != b->gen.b) mask |= 1u << BL_CFG_GATE;
    if (a->length != b->length) mask |= 1u << BL_CFG_LOOP;
    if (a->gen.direction != b->gen.direction) mask |= 1u << BL_CFG_DIRECTION;
    if (a->gen.groove != b->gen.groove) mask |= 1u << BL_CFG_GROOVE;
    if (a->phase != b->phase) mask |= 1u << BL_CFG_PHASE;
    if (a->density_cut != b->density_cut) mask |= 1u << BL_CFG_DENSITY;
    return mask;
}

static inline int bl_config_valid(const bl_vconfig_t *c)
{
    if (c->key.root < 0 || c->key.root > 11 || c->key.scale.mode >= BL_NMODES ||
        c->key.scale.count < 4 || c->key.scale.count > 12 || c->key.scale.off[0] ||
        c->gen.kind >= BL_NGENS || c->gen.direction >= BL_NDIRS || c->gen.groove >= BL_NGROOVES ||
        c->reg_low > c->reg_high || c->reg_high > 127 || c->length < BL_PPQN / 4 || c->length > INT32_MAX ||
        c->phase >= bl_config_phase_count(c) || c->density_cut > 100)
        return 0;
    for (int i = 1; i < c->key.scale.count; i++)
        if (c->key.scale.off[i] <= c->key.scale.off[i - 1] || c->key.scale.off[i] > 11) return 0;
    if (c->gen.kind == BL_GEN_ARP || c->gen.kind == BL_GEN_MOTIF)
        return (c->gen.a == 240 || c->gen.a == 480) && c->gen.b > 0 && c->gen.b < c->gen.a;
    /* Legacy generators use a as bars/accents and b as seed-procedure passes. */
    return c->gen.a >= 1 && c->gen.a <= 32 && c->gen.b >= 0 && c->gen.b <= 32;
}

/* Edit a copy, then validate: failure leaves the caller's settings untouched.
 * RATE/GATE/DIRECTION/GROOVE are currently meaningful only for ARP/MOTIF. */
static inline int bl_config_edit(bl_vconfig_t *c, int field, int32_t value)
{
    bl_vconfig_t next = *c;
    int arp = next.gen.kind == BL_GEN_ARP || next.gen.kind == BL_GEN_MOTIF;
    switch (field) {
    case BL_CFG_DENSITY:
        if (value < 0 || value > 100) return -1;
        next.density_cut = (uint8_t)(100 - value);
        break;
    case BL_CFG_GENERATOR:
        if (value < 0 || value >= BL_NGENS) return -1;
        if (next.gen.kind != value) {
            next.gen.kind = (uint8_t)value;
            int new_arp = value == BL_GEN_ARP || value == BL_GEN_MOTIF;
            if (!arp || !new_arp) {
                next.gen.a = new_arp ? 480 : 1;
                next.gen.b = new_arp ? 240 : value == BL_GEN_SEED_PROCEDURE ? 4 : 0;
            }
        }
        break;
    case BL_CFG_ROOT:
        if (value < 0 || value > 11) return -1;
        next.key.root = value;
        break;
    case BL_CFG_SCALE:
        if (value < 0 || value >= BL_NMODES) return -1;
        bl_scale_preset(value, &next.key.scale);
        break;
    case BL_CFG_REG_LOW:
        if (value < 0 || value > next.reg_high) return -1;
        next.reg_low = (uint8_t)value;
        break;
    case BL_CFG_REG_HIGH:
        if (value < next.reg_low || value > 127) return -1;
        next.reg_high = (uint8_t)value;
        break;
    case BL_CFG_RATE:
        if (!arp || (value != 240 && value != 480)) return -1;
        next.gen.a = (int16_t)value;
        if (next.gen.b >= value) next.gen.b = (int16_t)(value / 2);
        break;
    case BL_CFG_GATE:
        if (!arp || value < 1 || value >= next.gen.a) return -1;
        next.gen.b = (int16_t)value;
        break;
    case BL_CFG_LOOP:
        if (value < 1 || value > 16) return -1;
        next.length = (uint32_t)value * (BL_PPQN / 4);
        break;
    case BL_CFG_DIRECTION:
        if (!arp || value < 0 || value >= BL_NDIRS) return -1;
        next.gen.direction = (uint8_t)value;
        break;
    case BL_CFG_GROOVE:
        if (!arp || value < 0 || value >= BL_NGROOVES) return -1;
        next.gen.groove = (uint8_t)value;
        break;
    case BL_CFG_PHASE:
        if (value < 0 || value >= bl_config_phase_count(&next)) return -1;
        next.phase = (uint8_t)value;
        break;
    default: return -1;
    }
    /* A count-changing edit keeps the musical offset modulo the new source.
     * Explicit PHASE input is range-checked above rather than silently wrapped. */
    if (field != BL_CFG_PHASE) next.phase = (uint8_t)(next.phase % bl_config_phase_count(&next));
    if (!bl_config_valid(&next)) return -1;
    *c = next;
    return 0;
}
