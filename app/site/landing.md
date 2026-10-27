# BERLINAN

Berlinan, the generative Berlin School MIDI sequencer, as firmware for the M-VAVE FM-1. (fm1-base's
`tools/make_site.py` makes the site's front page from this file; `tools/site.sh` then puts Berlinan's own,
`app/site/index.html` and `app/site/cs/index.html`, in its place.)

Version {{VERSION}}. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Try it in the browser](emu/): the same code the FM-1 runs.
- [Manual](manual/): the controls, the pages, saving, sync.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 base/tools/fm1_install.py {{PKG}}`.
- [Source](https://github.com/jiripisa/fm1-berlinan): GPL-2.0-or-later. Built on fm1-base and felucca-bsp, from
  Felucca by Leo Kuroshita, Hügelton Instruments.

M-VAVE and FM-1 are trademarks of their owners. Berlinan is not affiliated with M-VAVE.
