/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jiří Píša */
/* A state (bl_state_t, Swift's StateSnapshot: the voices with their patterns, the generations, the schedule) as
 * bytes, for the flash's slots and the undo stack: a bl_state_t is 16.8 KB and holds its voices' ids as pointers,
 * the bytes hold only what the state holds, a voice by its id code (BL_VID_). Little-endian:
 *   n                                   the voices, then per voice:
 *   vid role reg_low reg_high ref_c     u8 each
 *   flags                               u8: participates 1, locked 2, anchor << 2
 *   generator remaining n               u8, i8 (-1: established), u8
 *   length start_offset generation      u32 each
 *   seed                                u64
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
