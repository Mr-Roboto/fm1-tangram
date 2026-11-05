/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "composer.h"
#include "legit.h"

/* In-place musical-frame edit, with caller-owned pattern scratch. No factory,
 * RNG draw or MIDI operation. Returns -1 refused, 0 unchanged, 1 changed. */
int bl_develop_voice(bl_voice_t *voice, const bl_voice_t *theme,
                     const bl_compmotion_t *target, bl_tick_t tick, bl_pattern_t *scratch);
