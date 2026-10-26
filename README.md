# Berlinan for the M-VAVE FM-1

Berlinan is a generative sequencer in the Berlin School tradition: a small ensemble of voices that repeat, phase
against each other on loops of unequal length, and earn their mutations over time. This is its firmware for the
M-VAVE FM-1, so the sequencer runs on its own box and needs no laptop. The FM-1 makes no sound: plug it by USB
into a MIDI router, and each voice goes out on its own MIDI channel to your synthesizers. You play it from the
panel: the keys, the buttons, the encoders and the screen.

It plays what Berlinan for macOS plays: the same template and seed, with the
same actions at the same bars, give the same notes and the same CC, event for event. Tests check this against the
unchanged Swift engine. It starts from Berlinan's built-in templates and a seed; projects do not move between the
Mac and the FM-1. The FM-1 is the MIDI clock master (clock, Start and Stop over USB), or follows an incoming
clock from USB or the TRS jack. SAVE keeps the state across power-off.

Status: the skeleton. It builds, boots and draws its name; the engine and the panel come next.

## Building and testing

[BUILDING.md](BUILDING.md) has the prerequisites. In short:

```
git submodule update --init --recursive
./build.sh                    # app/build/berlinan.fwsc
sh tests/run_tests.sh         # the host tests and the simulator scenarios
```

## Licence

GPL-2.0-or-later (see [LICENSE](LICENSE)). It is built on [fm1-base](https://github.com/charlesvestal/fm1-base)
(`base/`, MPL-2.0) and felucca-bsp, from Felucca by Leo Kuroshita (Hügelton Instruments); they keep their own
licences.
