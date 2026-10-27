#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Jiří Píša
"""Berlinan's own pages, over the site fm1-base's tools/make_site.py made (tools/site.sh runs both):

  index.html, cs/index.html     the front pages (app/site/index.html, app/site/cs/index.html), the version and the
                                package's file name filled in
  assets/                       their styles, script and icon (app/site/assets: Berlinan for macOS's site.css,
                                manual.css, phasing.js and icon.png, unchanged, and this site's fm1.css)
  img/                          the screenshots and the drawings of the panel (docs/img), for every page
  manual/, cs/manual/           the manuals (docs/MANUAL.md, docs/MANUAL.cs.md): fm1-base's tools/manual_html.py
                                turns the Markdown into HTML, set in Berlinan for macOS's manual layout
  install/, cs/install/         fm1-base's installer (base/web/installer.html, filled in and inlined as make_site.py
                                does it), in the site's colours, with the way back to the site; in Czech too
  emu/                          make_site.py's (the app's emu page, app/web/emu, with Web MIDI): left as it is, but for
                                the build's objects (emu/o2) that came with it

  tools/site_pages.py VERSION SITE_DIR      (SITE_DIR: make_site.py's output, with firmware/berlinan-VERSION.fwsc)

Last, every link and picture a page names inside the site is checked: the file is there, and so is the section
a #fragment names."""
import html
import json
import re
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
APP, BASE = SRC / "app", SRC / "base"
sys.path.insert(0, str(BASE / "tools"))
import manual_html as mh  # noqa: E402  (fm1-base's: the Markdown the manuals use, as HTML)
from make_site import product_of, read_conf, strip_module  # noqa: E402  (fm1-base's: the package format)

SITE_URL = "https://jiripisa.github.io/fm1-berlinan/"
BRANCH = "main"                                     # the public repository's branch, for links to its files
FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com">\n'
         '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n'
         '<link href="https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@62..125,400..800'
         '&family=JetBrains+Mono:wght@400;500;700&display=swap" rel="stylesheet">')

# the words around a manual and an installer; ROOT is the site's root from the page (manual/, cs/manual/, ...)
UI = {
    "en": {
        "root": "../", "other": "cs", "other_name": "Česky", "nav": "Site", "overview": "Overview",
        "manual": "Manual", "install": "Install", "emu": "Try it", "contents": "Contents", "skip": "Skip to content",
        "title": "Manual", "page_title": "Manual · Berlinan for the FM-1",
        "desc": "The manual of Berlinan for the M-VAVE FM-1: installing it, the controls, the seven pages, saving, "
                "sync, and how it differs from Berlinan for macOS.",
        "foot": "Berlinan for the FM-1 by Jiří Píša. GPL-2.0-or-later. The manual for version {v}.",
        "source": "Source on GitHub", "command": "Command", "table": "Table",
    },
    "cs": {
        "root": "../../", "other": "en", "other_name": "English", "nav": "Web", "overview": "Přehled",
        "manual": "Manuál", "install": "Instalace", "emu": "Vyzkoušet", "contents": "Obsah",
        "skip": "Přeskočit na obsah", "title": "Manuál", "page_title": "Manuál · Berlinan pro FM-1",
        "desc": "Manuál Berlinanu pro M-VAVE FM-1: instalace, ovládání, sedm stránek, ukládání, synchronizace "
                "a čím se liší od Berlinanu pro macOS.",
        "foot": "Berlinan pro FM-1, autor Jiří Píša. GPL-2.0-or-later. Manuál pro verzi {v}.",
        "source": "Zdrojový kód na GitHubu", "command": "Příkaz", "table": "Tabulka",
    },
}
PAGE = {"en": "", "cs": "cs/"}                        # where a language's pages live, from the root


def fill(path, version, pkg):
    return path.read_text(encoding="utf-8").replace("{{VERSION}}", html.escape(version)).replace("{{PKG}}", pkg)


def github_slug(title):
    """the anchor GitHub gives a heading (what the Markdown's own links use)"""
    return re.sub(r"[^\w\- ]", "", re.sub(r"[*`]", "", title).strip().lower()).replace(" ", "-")


def manual(md_path, out, lang, version, repo):
    """a manual: manual_html.py's body (tables, figures, callouts, code) in the layout of Berlinan for macOS's
    manual (masthead, title, contents beside the text), the pictures from the site's img/"""
    u, root = UI[lang], UI[lang]["root"]
    md = md_path.read_text(encoding="utf-8")
    md = re.sub(r"^# .+\n", "", md, count=1)                                         # the title: ours
    md = re.sub(r"^## (Contents|Obsah)\n.*?(?=^---$)", "", md, count=1, flags=re.M | re.S)   # the contents: ours
    body = mh.convert(md).replace("<hr>\n", "").replace("<hr>", "")
    body = body.replace('aria-label="Command', f'aria-label="{u["command"]}').replace('aria-label="Table', f'aria-label="{u["table"]}')
    body = body.replace('src="img/', f'src="{root}img/')

    def file_link(m):                                 # a file of the repository (../BUILDING.md): on GitHub
        href = m.group(1)
        if re.match(r"[a-z]+:|#", href):
            return m.group(0)
        path = (md_path.parent / href.split("#")[0]).resolve().relative_to(SRC)
        return f'href="{repo}/blob/{BRANCH}/{path}"'
    body = re.sub(r'href="([^"]+)"', file_link, body)
    ids = set(re.findall(r'\bid="([^"]+)"', body))
    slugs = {}                                         # GitHub's anchor of a heading -> its id here
    for m in re.finditer(r"^(##|###) (.+)$", md, re.M):
        t = m.group(2).strip()
        n = re.match(r"(\d+)\. ", t)
        slugs[github_slug(t)] = f"s{n.group(1)}" if m.group(1) == "##" and n else mh.slug(t)

    def anchor(m):
        a = m.group(1)
        if a in ids:
            return m.group(0)
        if slugs.get(a) not in ids:
            raise SystemExit(f"{md_path}: a link to #{a}: no such section")
        return f'href="#{slugs[a]}"'
    body = re.sub(r'href="#([^"]+)"', anchor, body)
    # the text before the first section is the title's; then a <section> a chapter, and the contents of both levels
    intro, _, rest = body.partition("<h2 ")
    chapters = ["<h2 " + c for c in rest.split("<h2 ")] if rest else []
    toc, article = [], []
    for c in chapters:
        h2 = re.match(r'<h2 id="([^"]+)">(.*?)</h2>', c)
        subs = re.findall(r'<h3 id="([^"]+)">(.*?)</h3>', c)
        sub = ("<ol>" + "".join(f'<li><a href="#{i}">{re.sub(r"<[^>]+>", "", t)}</a></li>' for i, t in subs) + "</ol>"
               if subs else "")
        toc.append(f'<li><a href="#{h2.group(1)}">{h2.group(2)}</a>{sub}</li>')
        article.append(f'<section aria-labelledby="{h2.group(1)}">\n{c.strip()}\n</section>')
    other_root = "../" if lang == "en" else "../../"
    other_href = other_root + PAGE[u["other"]] + "manual/"
    page = f"""<!doctype html>
<!-- made by tools/site_pages.py from docs/{md_path.name}: edit that, not this -->
<html lang="{lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(u["page_title"])}</title>
<meta name="description" content="{html.escape(u["desc"])}">
<meta name="theme-color" content="#131418">
<link rel="alternate" hreflang="en" href="{SITE_URL}manual/">
<link rel="alternate" hreflang="cs" href="{SITE_URL}cs/manual/">
<link rel="alternate" hreflang="x-default" href="{SITE_URL}manual/">
<link rel="icon" href="{root}assets/icon.png">
{FONTS}
<link rel="stylesheet" href="{root}assets/manual.css">
<link rel="stylesheet" href="{root}assets/fm1.css">
</head>
<body>
<a class="skip" href="#content">{u["skip"]}</a>
<header class="wrap masthead">
  <a class="brand" href="../"><img src="{root}assets/icon.png" alt="" width="32" height="32">Berlinan FM-1</a>
  <nav aria-label="{u["nav"]}">
    <a href="../">{u["overview"]}</a>
    <a href="./" aria-current="page">{u["manual"]}</a>
    <a href="../install/">{u["install"]}</a>
    <a href="{root}emu/">{u["emu"]}</a>
    <a href="{repo}">GitHub</a>
    <a class="lang" href="{other_href}" hreflang="{u["other"]}" lang="{u["other"]}">{u["other_name"]}</a>
  </nav>
</header>

<main id="content" tabindex="-1">
  <div class="wrap title">
    <h1>{u["title"]}</h1>
    {intro.strip()}
  </div>

  <div class="wrap layout">
    <nav class="toc" aria-label="{u["contents"]}">
      <p>{u["contents"]}</p>
      <ol>
        {chr(10).join(toc)}
      </ol>
    </nav>

    <article>
{chr(10).join(article)}
    </article>
  </div>
</main>

<footer>
  <div class="wrap">
    <span>{u["foot"].format(v=html.escape(version))}</span>
    <span><a href="../">{u["overview"]}</a> · <a href="{repo}">{u["source"]}</a></span>
  </div>
</footer>
</body>
</html>
"""
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding="utf-8")


# ---------------------------------------------------------------- the installer
# fm1-base's page, filled in as make_site.py fills it (the name as Berlinan writes it), in the site's colours: only
# styles and a header with the way back are added; the identity check, the install and the way back to M-VAVE's
# firmware are fm1-base's, unchanged. The Czech page adds a "cs" strings table and starts in it (Czech, English,
# 日本語 in turn on the language button).
THEME = """<style>
  /* Berlinan's site (assets/site.css): its night, its ink and its tangerine, over fm1-base's colours */
  :root:not(#berlinan) { --bg: #131418; --fg: #e7ecf2; --dim: #aab2bd; --accent: #FF9E4A; --on-accent: #1a1209;
                         --bad: #ff8a73; --focus: #FF9E4A; color-scheme: dark; }
  body { font-family: "Archivo", -apple-system, BlinkMacSystemFont, "Helvetica Neue", Arial, sans-serif; }
  h1, h2 { font-variation-settings: "wdth" 112; }
  .bl-mast { max-width: 520px; margin: 0 auto; padding: 24px 16px 0; display: flex; flex-wrap: wrap; gap: 8px 20px;
             align-items: center; justify-content: space-between; font-size: 15px; }
  .bl-mast a { color: var(--dim); text-decoration: none; }
  .bl-mast a:hover { color: var(--fg); }
  .bl-mast .bl-brand { display: flex; align-items: center; gap: 10px; color: var(--fg); font-weight: 600; }
  .bl-mast nav { display: flex; flex-wrap: wrap; gap: 4px 16px; }
  main { padding-top: 32px; }
</style>
"""
HEADER = """<header class="bl-mast">
  <a class="bl-brand" href="../"><img src="{root}assets/icon.png" alt="" width="28" height="28">Berlinan FM-1</a>
  <nav aria-label="{nav}"><a href="../manual/">{manual}</a> <a href="{root}emu/">{emu}</a> <a href="{other}" hreflang="{olang}" lang="{olang}">{oname}</a></nav>
</header>
"""
CS_TEXT = {
    "intro": "Připojte FM-1 přes USB a stiskněte Instalovat. Funguje v Chromu nebo Edgi; prohlížeč se jednou zeptá, "
             "jestli smí používat MIDI: povolte to.",
    "newtab": " (otevře se v nové kartě)", "progress": "Průběh", "pct": ", {n} procent",
    "warn": "Během zápisu neodpojujte USB kabel.",
    "install": "Instalovat", "other": "English",
    "manual": "Zpět na {{NAME}}",
    "direct": "USB kabel zapojte přímo do počítače. Žádný probe ani Transporter není potřeba.",
    "brick": "Kdyby FM-1 přestal startovat, Mac umí vrátit původní firmware:",
    "bricklink": "záchrana FM-1, který nenastartuje (anglicky)",
    "back": "K oficiálnímu firmwaru se vrátíte tlačítkem „Zpět k původnímu firmwaru“ níže, nebo vlastním updaterem "
            "M-VAVE, M-UPGRADE (Downloads → PC Software → M-UPGRADE).",
    "stockh": "Zpět k původnímu firmwaru",
    "stockwhy": "Zapíše zpátky vlastní firmware M-VAVE, FM-1 V15, z {{NAME}}u (nebo z jeho nouzového režimu či z jiného "
                "firmwaru se stejným vstupem pro aktualizace) přes stejný USB kabel. Co {{NAME}} uložil, se může ztratit.",
    "stock1": "Stáhněte FM-1 V15 od M-VAVE:",
    "stock2": "Vyberte stažený soubor:",
    "stock3": "Připojte FM-1 přes USB a stiskněte tlačítko níže.",
    "stockinstall": "Nainstalovat původní firmware",
    "stockok": "To je FM-1 V15 od M-VAVE. Stisknutím tlačítka ho nainstalujete.",
    "stockbad": "To není soubor FM-1 V15 od M-VAVE. Vyberte soubor z odkazu výše.",
    "stockdone": "Hotovo. FM-1 nastartoval s původním firmwarem.",
    "nomidi": "Tento prohlížeč to neumí. Otevřete stránku v Chromu nebo Edgi.", "loadfail": "Nepodařilo se načíst: ",
    "start": "Začínám", "verify": "Ověřuji", "loader": "Přepínám do režimu aktualizace", "write": "Zapisuji",
    "reboot": "Restartuji", "done": "Hotovo",
    "stopped": "Zastaveno. Zapojte USB kabel znovu a stiskněte Instalovat ještě jednou.",
    "error": "Chyba: ",
    "denied": "Přístup k MIDI (SysEx) nebyl povolen. Povolte MIDI v nastavení webu (v adresním řádku) a stiskněte "
              "Instalovat znovu.",
    "notfound": "FM-1 nebyl nalezen. Zkontrolujte USB kabel (musí být datový), zavřete ostatní aplikace, které "
                "používají MIDI, a stiskněte Instalovat znovu.",
    "model": "Připojené zařízení není FM-1.",
    "lost": "Spojení přes USB se přerušilo. Zapojte kabel znovu a stiskněte Instalovat ještě jednou.",
    "noloader": "FM-1 v režimu aktualizace nebyl nalezen. Zapojte USB kabel znovu a stiskněte Instalovat ještě jednou "
                "(v režimu aktualizace zůstane, dokud zápis nedokončí).",
    "noreturn": "FM-1 se nevrátil. Vypněte ho a znovu zapněte.",
    "mismatch": "Zapsáno, ale FM-1 hlásí jinou verzi: ",
    "badpkg": "Soubor k instalaci je poškozený. Načtěte stránku znovu.",
    "leave": "Probíhá zápis. Stránku nezavírejte.",
    "license": "{{LICENSE}}, bez jakékoli záruky. Zdrojový kód:",
    "reporth": "Nahlásit problém",
    "reportwhy": "Když FM-1 spadne, nevypínejte ho: připojte ho přes USB a stiskněte tlačítko níže. Záznam o pádu "
                 "přežije restart, ale ne vypnutí. Zkopírujte, co se objeví, a pošlete to spolu s tím, co jste zrovna "
                 "dělali.",
    "reportread": "Přečíst záznam o pádu", "reportcopy": "Kopírovat", "reportlabel": "Záznam o pádu",
    "reportwait": "Čtu…", "reportnone": "Žádná odpověď. Zkontrolujte, že na FM-1 běží {{NAME}}.",
    "reportok": "Přečteno. Zkopírujte to a pošlete.", "reportcopied": "Zkopírováno.",
}
CS_FOOTER = """<footer>
  <p class="small">{{NAME}} je postavený na fm1-base a felucca-bsp, z Feluccy od Leo Kuroshity (@kurogedelic), <a href="https://hugelton.com" target="_blank" rel="noopener">Hügelton Instruments<span class="sr" data-t="newtab"> (otevře se v nové kartě)</span></a>. Jeho engine je překladem Berlinanu pro macOS od Jiřího Píši do C, s aritmetikou z Berkeley SoftFloat.<br>
    Písma: Barlow Semi Condensed (SIL OFL 1.1). M-VAVE a FM-1 jsou ochranné známky svých vlastníků.<br>
    <span data-t="license"></span> <a href="{{REPO}}" target="_blank" rel="noopener">{{REPO_SHORT}}<span class="sr" data-t="newtab"> (otevře se v nové kartě)</span></a></p>
</footer>"""


def once(text, old, new, what):
    """old replaced by new, where old must be there exactly once (fm1-base's page as this script knows it)"""
    n = len(re.findall(old, text)) if isinstance(old, re.Pattern) else text.count(old)
    if n != 1:
        raise SystemExit(f"base/web/installer.html: {what}: found {n} times, not once (fm1-base's page changed?)")
    return old.sub(lambda m: new, text) if isinstance(old, re.Pattern) else text.replace(old, new)


def installer(lang, version, pkg_name, product, conf, out):
    root = UI[lang]["root"]
    page = (BASE / "web" / "installer.html").read_text(encoding="utf-8")
    page = once(page, "</style>\n", "</style>\n" + FONTS + "\n" + THEME, "the end of its styles")
    head = HEADER.format(root=root, nav=UI[lang]["nav"], manual=UI[lang]["manual"], emu=UI[lang]["emu"],
                         other=root + PAGE[UI[lang]["other"]] + "install/", olang=UI[lang]["other"],
                         oname=UI[lang]["other_name"])
    page = once(page, "<body>\n", "<body>\n" + head, "<body>")
    if lang == "cs":
        cs = "  cs: {\n" + "".join(f"    {k}: {json.dumps(v, ensure_ascii=False)},\n" for k, v in CS_TEXT.items()) + "  },\n"
        en_keys = re.findall(r'(?:^\s+|,\s+)(\w+): "', re.search(r"\n  en: \{\n(.*?)\n  \},", page, re.S).group(1),
                             re.M)
        if set(en_keys) != set(CS_TEXT):
            raise SystemExit(f"installer: the Czech strings differ from the English ones: {set(en_keys) ^ set(CS_TEXT)}")
        page = once(page, "const TEXT = {\n", "const TEXT = {\n" + cs, "its strings table")
        page = once(page, '<html lang="en">', '<html lang="cs">', "its language")
        page = once(page, "<title>Install · {{NAME}}</title>", "<title>Instalace · {{NAME}}</title>", "its title")
        page = once(page, "<h1>Install {{NAME}}</h1>", "<h1>Instalace {{NAME}}u</h1>", "its heading")
        page = once(page, '<button type="button" class="lang" id="lang" lang="ja">日本語</button>',
                    '<button type="button" class="lang" id="lang" lang="en">English</button>', "its language button")
        page = once(page, re.compile(r'let lang = "en";[^\n]*'),
                    'let lang = "cs";                                 /* Czech first (Berlinan\'s cs/install/); '
                    'English and 日本語 a click away */\nconst NEXT_LANG = { cs: "en", en: "ja", ja: "cs" };',
                    "its first language")
        page = once(page, '$("lang").lang = lang === "ja" ? "en" : "ja";', '$("lang").lang = NEXT_LANG[lang];',
                    "its language button's language")
        page = once(page, 'lang = lang === "ja" ? "en" : "ja"; applyLang();', "lang = NEXT_LANG[lang]; applyLang();",
                    "its language button")
        page = once(page, 'install: "インストール", other: "English",', 'install: "インストール", other: "Čeština",',
                    "the Japanese strings' other language")
        page = once(page, re.compile(r"<footer>.*?</footer>", re.S), CS_FOOTER, "its footer")
    # as make_site.py fills it in, then the library and the package's metadata
    lib = strip_module((BASE / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((BASE / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": root + "firmware/" + pkg_name})
    repo, credits = conf.get("repo", ""), conf.get("credits", "")
    for key, val in (("NAME", "Berlinan"), ("REPO", repo), ("REPO_SHORT", re.sub(r"^https?://", "", repo)),
                     ("LICENSE", conf.get("licence", "MPL-2.0")), ("CREDITS", " " + credits if credits else "")):
        page = page.replace("{{" + key + "}}", val)
    page = once(page, "/*LIB*/", lib, "/*LIB*/")
    page = once(page, "/*META*/", meta, "/*META*/")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding="utf-8")


# ---------------------------------------------------------------- the check
def check_links(site):
    """every href and src of every page that stays inside the site: its file is there, and its #fragment"""
    site, bad, ids = site.resolve(), [], {}
    pages = sorted(site.rglob("*.html"))
    for p in pages:
        ids[p] = set(re.findall(r'\bid="([^"]+)"', p.read_text(encoding="utf-8")))
    for p in pages:
        for ref in re.findall(r'\b(?:href|src)="([^"]+)"', p.read_text(encoding="utf-8")):
            if re.match(r"[a-z][a-z0-9+.-]*:|//", ref):
                continue
            path, _, frag = ref.partition("#")
            target = (p.parent / path).resolve() if path else p
            if path.endswith("/") or target.is_dir():
                target = target / "index.html"
            if not target.exists():
                bad.append(f"{p.relative_to(site)}: {ref}: no such file")
            elif frag and target.suffix == ".html" and frag not in ids.get(target, set()):
                bad.append(f"{p.relative_to(site)}: {ref}: no such section")
    if bad:
        raise SystemExit("site: broken links:\n  " + "\n  ".join(bad))
    return len(pages)


def main(version, site):
    site = Path(site)
    conf = read_conf(APP / "fm1.toml")
    pkg_name = f"{conf['name'].lower()}-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    pkg = site / "firmware" / pkg_name
    if not pkg.exists():
        raise SystemExit(f"no {pkg}: run fm1-base's tools/make_site.py first (tools/site.sh does)")
    product = product_of(pkg.read_bytes())
    for d in ("assets", "img", "manual", "cs", "emu/o2"):   # emu/o2: the emulator build's objects, copied with it
        if (site / d).exists():
            shutil.rmtree(site / d)
    shutil.copytree(APP / "site" / "assets", site / "assets")
    shutil.copytree(SRC / "docs" / "img", site / "img")
    (site / "cs").mkdir()
    (site / "index.html").write_text(fill(APP / "site" / "index.html", version, pkg_name), encoding="utf-8")
    (site / "cs" / "index.html").write_text(fill(APP / "site" / "cs" / "index.html", version, pkg_name),
                                            encoding="utf-8")
    manual(SRC / "docs" / "MANUAL.md", site / "manual" / "index.html", "en", version, conf["repo"])
    manual(SRC / "docs" / "MANUAL.cs.md", site / "cs" / "manual" / "index.html", "cs", version, conf["repo"])
    installer("en", version, pkg_name, product, conf, site / "install" / "index.html")
    installer("cs", version, pkg_name, product, conf, site / "cs" / "install" / "index.html")
    n = check_links(site)
    print(f"site: {site}: index.html, cs/, manual/, cs/manual/, install/ and cs/install/ ({product}), assets/, img/; "
          f"the links of {n} pages checked")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:3])
