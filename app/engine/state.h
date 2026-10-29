/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* A state (bl_state_t, Swift's StateSnapshot: the voices with their patterns, the generations, the schedule) as
 * bytes, for the flash's slots and the undo stack: a bl_state_t is about 17 KB and holds its voices' ids as pointers,
 * the bytes hold only what the state holds, a voice by its id code (BL_VID_). Little-endian:
 *   n                                   the voices, then per voice:
 *   vid role reg_low reg_high ref_c     u8 each
 *   flags                               u8: participates 1, locked 2, anchor << 2
 *   generator remaining n               u8, i8 (-1: established), u8
 *   length source-phase generation      u32 each (pat.start_offset holds source phase in a snapshot)
 *   seed                                u64
 *   active config, pending config       29 bytes each: root/mode/count/off[12], generator,
 *                                       a/b u16, direction/groove u8, length u32, register low/high/PHASE u8
 *                                       pat.pitch_phase is reconstructed from active PHASE
 *   pending field mask                  u16
 *   delay enabled time feedback decay   u8 each (all generators)
 *   MIDI channel                        u8, 0..15 (all generators)
 *   ARP/MOTIF only: direction cursor groove u8 each
 *   MOTIF only: interval_rng melody_rng u64 each; cycles_left scale_count chord[4] u8 each;
 *   local phase                         u32, active/blank flags u8 (all generators)
 *   n notes: position gate              u16 each
 *            degree octave accidental   i8 each
 *            velocity flags             u8, u8: accent 1, ratchet << 1
 *   events                              u16, then per event the bar as LEB128 steps from the one before (the first
 *                                       from bar 0) and the offset, i8
 * A value the format cannot hold (a loop of more than 65,535 ticks, an id that is none of BL_VID_, ...) is refused,
 * never cut; so is, both ways, what no engine state holds: a loop shorter than a 16th, a generator, velocity (1-127),
 * accidental (-2...2) or ratchet (1-3) out of its range. */
#pragma once
#include <stdint.h>
#include "live.h"

/* the bytes of a state into buf: their number, or -1 when they do not fit in max or the state holds a value the
 * format cannot */
int state_pack(const bl_state_t *s, uint8_t *buf, int max);
/* n bytes back into a state: 0, or -1 when they are not one (short, too long, a value no engine state has) */
int state_unpack(const uint8_t *buf, int n, bl_state_t *out);
