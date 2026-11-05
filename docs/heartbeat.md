# Heartbeat drums

HEARTBEAT is one MIDI drum voice with four fixed General MIDI instruments:

| Instrument | MIDI note | Pattern label |
| --- | --- | --- |
| Kick | 36 | K |
| Snare | 38 | S |
| Closed hi-hat | 42 | H |
| Open hi-hat | 46 | O |

The Full Ensemble template uses MIDI channel 10. Manual channel settings remain
authoritative; use a drum kit on the receiving instrument. No additional tracks
are created.

Each four-beat loop has quarter-note kicks, snares on beats 2 and 4, eighth-note
hats, and restrained seeded sixteenth-note hat additions. One open hat replaces
a closed hat on an offbeat. Regeneration repeats deterministically from its seed.
Existing accent and ratchet evolution remains available; automatic pitch mutation
and time nudging are excluded to preserve the instruments and basic pulse.
LOCK, mute, stop/start and existing MIDI delay behavior remain in force.

Drum gates have a fixed 50% duty cycle: kicks/snares use half a quarter note
(480 ticks), regular eighth-note hats half an eighth (240 ticks), and added
sixteenth-note hats half a sixteenth (120 ticks), at 960 ticks per quarter.
Open hats use the same eighth-note gate as closed hats at that subdivision.
Ratchet hits use half their existing sixteenth-note retrigger slice (60 ticks
for two hits, 40 for three). Existing paired Note-Off scheduling is retained;
melodic gates are unaffected.

When HEARTBEAT is selected, HOME QUICK replaces Root/Scale with two drum controls:

| Encoder | Selection | Meaning |
| --- | --- | --- |
| K3 PARTS | 1 | BD only |
| | 2 | BD + SD |
| | 3 | BD + SD + CH |
| | 4 ALL (default) | BD + SD + CH + OH |
| K4 GROOVE | 1 SPARSE | Thin the existing hats to quarter-note positions; retain kicks and backbeat |
| | 2 STEADY (default) | Preserve the established seeded pattern |
| | 3 DRIVE | Increase the existing odd-sixteenth hat probability from 1/4 to 1/2 |

Both selectors clamp at their ends. PARTS is an instrument permission mask,
not a density setting: excluded instruments are removed from the pattern and
rejected again at Note-On rendering. Neither density nor Composer activity
can re-enable them. Permitted instruments may still be silent through density
or the selected groove; SPARSE has no open-hat event. Density uses its existing
filter and ranking on the permitted pattern. No new drum algorithm or voice is
introduced. The three grooves share their seed stream and retain the basic pulse.
Groove changes affect hats only, so BD-only and BD+SD selections keep the same
rhythm across all three groove choices.

Edits while playing take effect at the voice's next local loop boundary using
the existing configuration path, preserving LOCK and the same pattern seed.
Manual controls retain the existing Composer override behavior. Existing
Note-Offs and captured MIDI delay tails finish normally. Normal SAVE stores both
controls with the project; snapshots and undo retain them too. The existing
generator configuration bytes `direction` and `groove` carry Heartbeat PARTS
and GROOVE respectively (zero = ALL / STEADY). ARP/MOTIF keep their existing
meanings. No fields, storage format or migration code are added.

ENV ROUTING continues to hide K2 Register. Global root/scale settings and other
voices' controls remain available as before.
ARP HARMONY hides/disables K2 Root and K3 Scale. ALGORITHM on that page is also
inactive for Heartbeat, so it cannot change global Scale through an alternate
input path. HOME QUICK retains PARTS/GROOVE instead of Root/Scale. These guards
follow the selected generator, including pending configuration changes.
ENV TIMING shows Gate as read-only `50% / FIXED`. MIDI channel, density, mute
and the other valid controls retain their mappings. ENV shows `BD SD CH OH`
in its otherwise unused CC row; voices with CC configuration retain that row.
These abbreviations mean bass drum, snare drum, closed hat and open hat.

Key, scale, harmonic transposition, reference octave, register windows and
Composer pitch/register development do not retune these notes. The melodic
TENSION one-shot is ignored when addressed to HEARTBEAT, without spending its
budget. Arranger root thinning retains kick notes instead of retuning drums.

MASTER, per-voice density and Composer section activity use their existing
combined filtering. Kicks are initially accented to prioritize the pulse at
reduced density. Existing accent evolution can change that priority. Density
zero silences new hits while musical time and loop phase continue.

Pattern strips show fixed drum heights independent of key. The step grid labels
single-instrument onsets K/S/H/O and simultaneous instruments with `+`. A combined
cell is active if any hit survives density filtering; strips retain individual
suppressed bars. Existing red source markers and grey timing cursors are retained.

Focused regression source: `tests/host/heartbeat_test.c`, with standalone runner
`sh tests/host/run_heartbeat.sh`. It covers exact rendered MIDI notes across all
keys and preset scales, transposition, excluded register ranges, default/manual
channels, pulse/backbeat, mutation, LOCK, thinning, Composer B and zero density.
It also covers the fixed drum gates, ratchet sub-gates and unchanged melodic
gates, default STEADY material, all PARTS/GROOVE combinations, density permission
enforcement, Composer ownership/LOCK, and snapshot storage. `test_drum_controls`
in `tests/host/scale_app_test.c` covers selector boundaries, unchanged global
settings, melodic mappings, and project SAVE/load of PARTS/GROOVE.
These tests were added without compilation or execution; hardware behavior is
unverified.
