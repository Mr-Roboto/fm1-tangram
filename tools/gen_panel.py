#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal (AMB-1's tools/gen_manual_figures.py)
# Copyright (C) 2026 Jiří Píša (Berlinan's labels, the key groups, the Czech drawing)
"""The map of the controls, drawn (no photographs: M-VAVE's and Felucca's are not ours to use), for the manuals and
the site:

  docs/img/fm1-panel.svg      the FM-1 front panel, Berlinan's real screen in it, each control labelled with what it
                              does in Berlinan, the keys with their notes and their groups under them
  docs/img/fm1-panel.cs.svg   the same, labelled in Czech

  tools/gen_panel.py [SCREEN.png]     (default docs/img/screen-perform.png; needs Pillow)

AMB-1's drawing (tools/gen_manual_figures.py there): two knob pairs and the octave buttons on the left, the screen
in the middle, four knobs and two rows of six buttons on the right, and the keys below: 11 black keys in groups of
3, 2, 3, 2, 1 over 16 white keys (F3 to G5). The words under the controls are in Berlinan's tangerine."""
import base64
import io
import sys
from pathlib import Path

from PIL import Image

SRC = Path(__file__).resolve().parents[1]
OUT = SRC / "docs" / "img"
FONT = "'Archivo', 'Helvetica Neue', Helvetica, Arial, sans-serif"

BODY, TRAY, KEY_W, KEY_B, KNOB, BTN, LABEL = "#3a3d42", "#26282c", "#d9dadc", "#c9cacd", "#1f2124", "#2e3135", "#e8e8ea"
WHAT = "#FF9E4A"                                       # Berlinan's tangerine (its site's --tangerine)
WHITE_N = 16
BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]    # black key k sits after white key BLACK_AFTER[k]
WHITE_NOTES = "F G A B C D E F G A B C D E F G".split()
BLACK_NOTES = "F# G# A# C# D# F# G# A# C# D# F#".split()
# a group of keys (ui.c): its white keys (first, last), its colour (the Mac's voice colours), and its words
GROUPS = [(0, 3, "#5FB8C9", "voices"), (4, 10, "#C9975F", "degrees"), (11, 14, "#9B7FC9", "slots"),
          (15, 15, "#C96F6F", "tension")]
BLACK_GROUP = [0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 3]       # the group each black key belongs to (F#5, >>: with tension)

WORDS = {
    "en": {
        "label": "The FM-1 front panel running Berlinan, each control labelled with what it does",
        "MASTER": "Density", "SELECT": "Voice|HOME + it: tempo", "PRESETS": "Template", "ALGORITHM": "Mode",
        "OCT-": "Undo", "OCT+": "Dice (new seed)", "knobs": "The page's four values",
        "FX": "Delay", "SEL": "Lock", "ENV": "Voice", "LFO": "CC", "EDIT": "Re-roll", "GLO": "Setup",
        "HOME": "Perform", "SAVE": "Save", "ARP": "Pattern", "SEQ": "Arranger", "PLAY": "Play /|stop",
        "REC": "B part",
        "voices": "Voices: tap mutes,|hold selects", "degrees": "Degrees 1 to 7 (white)|scenes 1 to 5 (black)",
        "slots": "Snapshot slots 1 to 6|SAVE + slot: store", "tension": ">>, tension",
        "foot": "Hold any button but PLAY a second: what it does",
    },
    "cs": {
        "label": "Přední panel FM-1 s Berlinanem, u každého ovládacího prvku, co dělá",
        "MASTER": "Hustota", "SELECT": "Hlas|HOME + on: tempo", "PRESETS": "Šablona", "ALGORITHM": "Mód",
        "OCT-": "Zpět (undo)", "OCT+": "Dice (nový seed)", "knobs": "Čtyři hodnoty stránky",
        "FX": "Delay", "SEL": "Zámek", "ENV": "Hlas", "LFO": "CC", "EDIT": "Re-roll", "GLO": "Setup",
        "HOME": "Perform", "SAVE": "Uložit", "ARP": "Pattern", "SEQ": "Aranžér",
        "PLAY": "Play /|stop", "REC": "B-díl",
        "voices": "Hlasy: ťuknutí ztlumí,|podržení vybere", "degrees": "Stupně 1 až 7 (bílé)|scény 1 až 5 (černé)",
        "slots": "Sloty snapshotů 1 až 6|SAVE + slot: uložit", "tension": ">>, napětí",
        "foot": "Podržte kterékoli tlačítko kromě PLAY na vteřinu: řekne, co dělá",
    },
}


def png_data_uri(path):
    im = Image.open(path).convert("RGB")
    b = io.BytesIO()
    im.save(b, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(b.getvalue()).decode()


def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def what(x, y, t, cls="what"):
    """what a control does, under it (one or two lines, split at |)"""
    a, _, b = t.partition("|")
    return (f'<text x="{x}" y="{y}" text-anchor="middle" class="{cls}">{esc(a)}</text>' +
            (f'<text x="{x}" y="{y + 15}" text-anchor="middle" class="{cls}">{esc(b)}</text>' if b else ""))


def knob(x, y, r, label, does=None):
    """a knob: its name above, what it does in Berlinan below"""
    return (f'<g><circle cx="{x}" cy="{y}" r="{r + 5}" fill="{TRAY}"/>'
            f'<circle cx="{x}" cy="{y}" r="{r}" fill="{KNOB}" stroke="#44474c" stroke-width="2"/>'
            f'<line x1="{x}" y1="{y - r + 5}" x2="{x}" y2="{y - r + 15}" stroke="{LABEL}" stroke-width="3" stroke-linecap="round"/>'
            f'<text x="{x}" y="{y - r - 12}" text-anchor="middle" class="lbl">{label}</text>'
            + (what(x, y + r + 22, does) if does else "") + '</g>')


def button(x, y, w, h, label, two=None):
    t = (f'<text x="{x + w / 2}" y="{y + h / 2 + 5}" text-anchor="middle" class="btn">{label}</text>' if not two else
         f'<text x="{x + w / 2}" y="{y + h / 2 - 1}" text-anchor="middle" class="btn small">{label}</text>'
         f'<text x="{x + w / 2}" y="{y + h / 2 + 12}" text-anchor="middle" class="btn small">{two}</text>')
    return f'<g><rect x="{x}" y="{y}" width="{w}" height="{h}" rx="9" fill="{BTN}" stroke="#4a4d52" stroke-width="1.5"/>{t}</g>'


def keybed(x0, y0, w, h, words):
    """the 16 white keys and 11 black keys as pills, each with its note and a stripe of its group's colour; under
    them, each group's span and words"""
    s = [f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="18" fill="{TRAY}"/>']
    pitch = (w - 24) / WHITE_N
    kw = pitch * 0.78
    colour = {i: g[2] for g in GROUPS for i in range(g[0], g[1] + 1)}
    for i in range(WHITE_N):
        x = x0 + 12 + i * pitch + (pitch - kw) / 2
        y, kh = y0 + h * 0.43, h * 0.52
        s.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{kw:.1f}" height="{kh:.1f}" rx="{kw / 2:.1f}" fill="{KEY_W}"/>')
        s.append(f'<rect x="{x + kw * 0.3:.1f}" y="{y + kh - 22:.1f}" width="{kw * 0.4:.1f}" height="6" rx="3" fill="{colour[i]}"/>')
        s.append(f'<text x="{x + kw / 2:.1f}" y="{y + kh * 0.55:.1f}" text-anchor="middle" class="key">{WHITE_NOTES[i]}</text>')
    for k, a in enumerate(BLACK_AFTER):
        x = x0 + 12 + (a + 1) * pitch - kw / 2
        y, kh = y0 + h * 0.06, h * 0.33
        s.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{kw:.1f}" height="{kh:.1f}" rx="{kw / 2:.1f}" fill="{KEY_B}"/>')
        s.append(f'<rect x="{x + kw * 0.3:.1f}" y="{y + kh - 16:.1f}" width="{kw * 0.4:.1f}" height="6" rx="3" fill="{GROUPS[BLACK_GROUP[k]][2]}"/>')
        s.append(f'<text x="{x + kw / 2:.1f}" y="{y + kh * 0.5:.1f}" text-anchor="middle" class="key small">{BLACK_NOTES[k]}</text>')
    yb = y0 + h + 14
    for first, last, col, key in GROUPS:                  # the group's span, then its words
        xa, xb = x0 + 12 + first * pitch + 4, x0 + 12 + (last + 1) * pitch - 4
        s.append(f'<rect x="{xa:.1f}" y="{yb}" width="{xb - xa:.1f}" height="4" rx="2" fill="{col}"/>')
        s.append(what(round((xa + xb) / 2, 1), yb + 22, words[key], "grp"))
    return "".join(s)


def style():
    return (f"<style>.lbl{{font:600 16px {FONT};fill:{LABEL};letter-spacing:.06em}}"
            f".btn{{font:600 14px {FONT};fill:{LABEL};letter-spacing:.05em}}.btn.small{{font-size:11px}}"
            f".key{{font:600 15px {FONT};fill:#2a2c30}}.key.small{{font-size:12px}}"
            f".what{{font:500 13px {FONT};fill:{WHAT}}}.grp{{font:500 14px {FONT};fill:{LABEL}}}"
            f".foot{{font:400 13px {FONT};fill:#aab2bd}}</style>")


def panel(screen_png, lang):
    w = WORDS[lang]
    W, H = 1000, 790
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" role="img" aria-label="{esc(w["label"])}">',
         style(), f'<rect x="4" y="4" width="{W - 8}" height="{H - 8}" rx="44" fill="{BODY}"/>',
         knob(105, 92, 24, "MASTER", w["MASTER"]), knob(215, 92, 24, "SELECT", w["SELECT"]),
         knob(105, 210, 24, "PRESETS", w["PRESETS"]), knob(215, 210, 24, "ALGORITHM", w["ALGORITHM"]),
         f'<rect x="58" y="284" width="210" height="58" rx="14" fill="{TRAY}"/>',
         button(72, 294, 82, 38, "OCT-"), button(170, 294, 82, 38, "OCT+"),
         what(113, 362, w["OCT-"]), what(211, 362, w["OCT+"]),
         f'<rect x="300" y="44" width="236" height="236" rx="26" fill="#111214"/>',
         f'<image x="318" y="62" width="200" height="200" href="{png_data_uri(screen_png)}" style="image-rendering:pixelated"/>']
    for i in range(4):
        s.append(knob(610 + i * 104, 92, 22, f"KNOB{i + 1}"))
    s.append(what(766, 136, w["knobs"]))
    s.append(f'<rect x="568" y="146" width="400" height="236" rx="18" fill="{TRAY}"/>')
    top = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"]
    bot = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]
    for i in range(6):
        x = 582 + i * 63
        s.append(button(x, 158, 54, 48, top[i]))
        s.append(what(x + 27, 224, w[top[i]]))
        s.append(button(x, 264, 54, 48, "PLAY", "STOP") if bot[i] == "PLAY" else button(x, 264, 54, 48, bot[i]))
        s.append(what(x + 27, 330, w[bot[i]]))
    s.append(keybed(36, 400, W - 72, 270, w))
    s.append(f'<text x="{W / 2}" y="{H - 22}" text-anchor="middle" class="foot">{esc(w["foot"])}</text>')
    s.append("</svg>")
    return "".join(s)


def main():
    screen = Path(sys.argv[1]) if len(sys.argv) > 1 else OUT / "screen-perform.png"
    for lang, name in (("en", "fm1-panel.svg"), ("cs", "fm1-panel.cs.svg")):
        (OUT / name).write_text(panel(screen, lang) + "\n", encoding="utf-8")
        print(f"figure: {OUT / name}")


if __name__ == "__main__":
    main()
