#!/usr/bin/env python3
"""Render a Markdown manual to a styled PDF (and HTML) in the manner of a vendor user guide.

    build-manual.py MANUAL.md OUT_DIR [VERSION] [--config docs/manual.json] [--root REPO]

Writes OUT_DIR/<Product>-Manual.pdf, .html and the intermediate .typ. pandoc
turns the Markdown into Typst source, this script restyles it (cover, header
band, grey section bars, coloured callouts, keycaps for bold ALL-CAPS control
names, breakable tables with sensible column widths) and typst compiles it.
`brew install pandoc typst`. Without them the script says so and exits 0 so
the rest of a build is not blocked.

Configuration lives in a JSON file (default docs/manual.json next to the repo
root, else built-in defaults); see templates/manual.json in the skill.
"""
import json
import math
import os
import random
import re
import shutil
import subprocess
import sys

DEFAULTS = {
    "product": "Product",
    "title_plain": "PROD",
    "title_accent": "UCT",
    "tagline": "",
    "band_right": "",
    "subtitle": "User Guide",
    "disclaimer": "",
    "accent": "#ffb000",
    "callout": "#c8102e",
    "dark": "#1f2126",
    "paper": "#e8e6e1",
    "body_font": ["Avenir Next", "Avenir", "Helvetica Neue", "Helvetica"],
    "cap_font": ["Helvetica Neue", "Helvetica", "Arial"],
    "image_root": "docs",
    "image_width": "60%",
    "two_column_widths": [30, 70],
    "four_column_widths": [16, 28, 28, 28],
    "cover": "mosaic",
}


def typst_font_list(fonts):
    return "(" + ", ".join(json.dumps(f) for f in fonts) + ")"


def header(cfg):
    return r"""
#let accent = rgb("%(accent)s")
#let callout = rgb("%(callout)s")
#let dark = rgb("%(dark)s")
#let paper = rgb("%(paper)s")
#let body-font = %(body_font)s
#let cap-font = %(cap_font)s

// A control name drawn as a keycap: grey cap with the label, the way vendor guides show buttons.
#let key(body) = box(
  fill: gradient.linear(luma(250), luma(222), angle: 90deg), stroke: 0.6pt + luma(150), radius: 2.5pt,
  inset: (x: 4pt, y: 1.2pt), outset: (y: 1pt), baseline: 22%%,
  text(font: cap-font, size: 8.2pt, weight: 700, fill: luma(40), tracking: 0.2pt)[#body])
#let keys(..items) = items.pos().map(key).join(text(size: 8pt, weight: 700)[ + ])

// Page furniture after the cover: a dark band with the wordmark, a page number.
#let band = block(width: 100%%, height: 11mm, fill: dark, inset: (x: 10mm))[
  #place(left + horizon)[#text(font: cap-font, size: 13pt, weight: 800, fill: accent, tracking: 1pt)[%(title_plain)s#text(fill: callout.lighten(25%%))[%(title_accent)s]]]
  #place(right + horizon)[#text(font: cap-font, size: 8.5pt, weight: 600, fill: luma(200), tracking: 1.6pt)[%(band_right)s]]
]
#let page-settings = (
  paper: "a4", margin: (top: 22mm, bottom: 18mm, left: 16mm, right: 16mm),
  header-ascent: 0pt, header: place(top + left, dx: -16mm, dy: 0mm, box(width: 210mm)[#band]),
  footer: context [#set text(size: 8pt, fill: luma(130)); #h(1fr) #counter(page).display()],
)

#set text(font: body-font, size: 10pt, fill: luma(25))
#set par(leading: 0.6em, justify: true)
#set list(marker: text(fill: dark)[•], indent: 2mm, body-indent: 2mm)
#set enum(indent: 2mm, body-indent: 2mm)

// Section bars: a full-width grey bar for chapters, a lighter one for sub-sections.
#show heading.where(level: 1): it => block(above: 12pt, below: 8pt, width: 100%%, fill: luma(215), inset: (x: 3mm, y: 2.2mm))[
  #set text(font: body-font, size: 14pt, weight: 700, fill: luma(20)); #it.body]
#show heading.where(level: 2): it => block(above: 12pt, below: 8pt, width: 100%%, fill: luma(215), inset: (x: 3mm, y: 2.2mm))[
  #set text(font: body-font, size: 14pt, weight: 700, fill: luma(20)); #it.body]
#show heading.where(level: 3): it => block(above: 10pt, below: 6pt, width: 100%%, fill: luma(236), inset: (x: 3mm, y: 1.6mm))[
  #set text(font: body-font, size: 11.5pt, weight: 700, fill: luma(20)); #it.body]

// Blockquotes are the callout strips (Tip, Note, Warning).
#show quote.where(block: true): it => block(width: 100%%, fill: callout, inset: (x: 3mm, y: 1.8mm), above: 6pt, below: 8pt)[
  #set text(fill: white, size: 9.5pt); #set par(justify: false); #it.body]

#show figure: set block(breakable: true)
#show figure: set align(left)
#show figure.where(kind: image): set align(center)
#show figure.caption: set text(size: 8.5pt, fill: luma(90))
#show image: set image(width: %(image_width)s)
#show table: set text(size: 9.2pt)
#set table(stroke: (x, y) => if y > 0 { (top: 0.5pt + luma(210)) }, inset: (x: 6pt, y: 4.2pt),
  fill: (x, y) => if y == 0 { luma(226) }, align: left + top)
#show table.cell.where(y: 0): set text(weight: 700)
#show raw.where(block: false): it => box(fill: luma(238), inset: (x: 2pt, y: 0pt), outset: (y: 2pt), radius: 2pt, text(size: 9pt, it))
#show raw.where(block: true): it => block(fill: luma(238), inset: 8pt, radius: 3pt, width: 100%%, text(size: 8.6pt, it))
#show link: set text(fill: rgb("#0b4ea2"))
#show strong: set text(weight: 700)
""" % {
        "accent": cfg["accent"], "callout": cfg["callout"], "dark": cfg["dark"], "paper": cfg["paper"],
        "body_font": typst_font_list(cfg["body_font"]), "cap_font": typst_font_list(cfg["cap_font"]),
        "title_plain": cfg["title_plain"], "title_accent": cfg["title_accent"], "band_right": cfg["band_right"],
        "image_width": cfg["image_width"],
    }


SHAPES = ["ring", "disc", "square", "stripes", "triangle", "dots", "wave", "squares", "half", "cross", "bars", "diamond"]


def tile(shape, x, y, s, invert):
    bg = "luma(20)" if invert else "none"
    fg = "paper" if invert else "luma(20)"
    out = [f'#place(top + left, dx: {x}mm, dy: {y}mm, rect(width: {s}mm, height: {s}mm, fill: {bg}))']
    cx, cy = x + s / 2, y + s / 2
    if shape == "ring":
        out.append(f'#place(top + left, dx: {cx - 9}mm, dy: {cy - 9}mm, circle(radius: 9mm, stroke: 2.4mm + {fg}))')
    elif shape == "disc":
        out.append(f'#place(top + left, dx: {cx - 8}mm, dy: {cy - 8}mm, circle(radius: 8mm, fill: {fg}))')
    elif shape == "square":
        out.append(f'#place(top + left, dx: {cx - 9}mm, dy: {cy - 9}mm, rect(width: 18mm, height: 18mm, stroke: 2.4mm + {fg}))')
    elif shape == "stripes":
        for i in range(6):
            out.append(f'#place(top + left, dx: {x + 3}mm, dy: {y + 3 + i * 4.3:.1f}mm, rect(width: {s - 6}mm, height: 1.6mm, fill: {fg}))')
    elif shape == "triangle":
        out.append(f'#place(top + left, dx: {x}mm, dy: {y}mm, polygon(fill: {fg}, ({s/2}mm, 4mm), ({s-4}mm, {s-4}mm), (4mm, {s-4}mm)))')
    elif shape == "dots":
        for i in range(4):
            for j in range(4):
                out.append(f'#place(top + left, dx: {x + 4.5 + i * 6.4:.1f}mm, dy: {y + 4.5 + j * 6.4:.1f}mm, circle(radius: 1.6mm, fill: {fg}))')
    elif shape == "wave":
        pts = [(x + 3 + k * (s - 6) / 24, cy + math.sin(k / 24 * math.pi * 3) * 7) for k in range(25)]
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            out.append(f'#place(top + left, line(start: ({ax:.2f}mm, {ay:.2f}mm), end: ({bx:.2f}mm, {by:.2f}mm), stroke: 2mm + {fg}))')
    elif shape == "squares":
        for d in (11, 7, 3):
            out.append(f'#place(top + left, dx: {cx - d}mm, dy: {cy - d}mm, rect(width: {2*d}mm, height: {2*d}mm, stroke: 1.4mm + {fg}))')
    elif shape == "half":
        out.append(f'#place(top + left, dx: {cx - 9}mm, dy: {cy - 9}mm, circle(radius: 9mm, fill: {fg}))')
        cover_fill = "luma(20)" if invert else "paper"
        out.append(f'#place(top + left, dx: {cx}mm, dy: {cy - 10}mm, rect(width: 10mm, height: 20mm, fill: {cover_fill}))')
    elif shape == "cross":
        out.append(f'#place(top + left, dx: {cx - 1.5}mm, dy: {y + 4}mm, rect(width: 3mm, height: {s - 8}mm, fill: {fg}))')
        out.append(f'#place(top + left, dx: {x + 4}mm, dy: {cy - 1.5}mm, rect(width: {s - 8}mm, height: 3mm, fill: {fg}))')
    elif shape == "bars":
        for i in range(5):
            h = 6 + ((i * 7) % 16)
            out.append(f'#place(top + left, dx: {x + 4 + i * 4.8:.1f}mm, dy: {y + s - 4 - h}mm, rect(width: 2.6mm, height: {h}mm, fill: {fg}))')
    elif shape == "diamond":
        out.append(f'#place(top + left, dx: {x}mm, dy: {y}mm, polygon(fill: {fg}, ({s/2}mm, 4mm), ({s-4}mm, {s/2}mm), ({s/2}mm, {s-4}mm), (4mm, {s/2}mm)))')
    return "\n  ".join(out)


def cover(cfg, version):
    """The cover: a seeded tile mosaic (or plain paper) with a title card."""
    tiles = []
    if cfg["cover"] == "mosaic":
        rnd = random.Random(7)
        cols, rows, size = 7, 10, 30
        for r in range(rows):
            for c in range(cols):
                tiles.append(tile(rnd.choice(SHAPES), c * size, r * size, size, rnd.random() < 0.3))
    return r"""
#page(paper: "a4", margin: 0pt, header: none, footer: none)[
  #place(top + left, rect(width: 210mm, height: 297mm, fill: paper))
  %(tiles)s
  #place(top + left, dx: 14mm, dy: 44mm, box(width: 182mm, fill: paper, stroke: 1.2pt + luma(20), inset: (x: 10mm, y: 8mm))[
    #text(font: cap-font, size: 46pt, weight: 900, fill: luma(20), tracking: 2pt)[%(title_plain)s#text(fill: callout)[%(title_accent)s]]
    #v(-3mm)
    #text(font: body-font, size: 13pt, weight: 600, fill: luma(40))[%(tagline)s]
    #v(1mm)
    #text(font: body-font, size: 10pt, fill: luma(80))[%(subtitle)s · %(version)s]
  ])
  #place(bottom + left, dx: 14mm, dy: -10mm, box(fill: paper, inset: 3mm, text(font: body-font, size: 7.5pt, fill: luma(90))[%(disclaimer)s]))
]
""" % {
        "tiles": "\n  ".join(tiles), "title_plain": cfg["title_plain"], "title_accent": cfg["title_accent"],
        "tagline": cfg["tagline"], "subtitle": cfg["subtitle"], "version": version, "disclaimer": cfg["disclaimer"],
    }


# Bold ALL-CAPS names (letters, digits, spaces, slashes, dashes), optionally joined with " + ".
KEY_RE = re.compile(r"#strong\[((?:[A-Z0-9][A-Z0-9 /\-–\n]*?)(?: \+ [A-Z0-9][A-Z0-9 /\-–\n]*?)*)\]")


def keycaps(src):
    """**SHIFT** becomes a keycap; **SHIFT + PAD 16** becomes two keycaps with a plus."""
    def repl(m):
        label = re.sub(r"\s+", " ", m.group(1)).strip()
        if not re.search(r"[A-Z]", label):
            return m.group(0)
        parts = [p.strip() for p in label.split(" + ")]
        if len(parts) == 1:
            return f"#key[{parts[0]}]"
        return "#keys(" + ", ".join(f"[{p}]" for p in parts) + ")"
    return KEY_RE.sub(repl, src)


def column_widths(body, cfg):
    """pandoc gives every table equal columns; a Control | Action table wants a narrow first column."""
    two = cfg["two_column_widths"]
    four = cfg["four_column_widths"]
    body = body.replace("columns: (50%, 50%)", f"columns: ({two[0]}%, {two[1]}%)")
    body = body.replace("columns: (25%, 25%, 25%, 25%)", "columns: (" + ", ".join(f"{w}%" for w in four) + ")")
    body = re.sub(r"align: \((?:auto,)+\)", lambda m: m.group(0).replace("auto", "left"), body)
    return body


def main():
    args = [a for a in sys.argv[1:]]
    config_path, root = None, None
    for flag in ("--config", "--root"):
        if flag in args:
            i = args.index(flag)
            value = args[i + 1]
            del args[i:i + 2]
            if flag == "--config":
                config_path = value
            else:
                root = value
    if len(args) < 2:
        sys.exit(__doc__)
    src, out_dir = args[0], args[1]
    version = args[2] if len(args) > 2 else "dev"
    root = os.path.abspath(root or os.path.dirname(os.path.abspath(src)))
    cfg = dict(DEFAULTS)
    config_path = config_path or os.path.join(root, "docs", "manual.json")
    if os.path.exists(config_path):
        with open(config_path, encoding="utf-8") as f:
            cfg.update(json.load(f))
    os.makedirs(out_dir, exist_ok=True)
    pandoc, typst = shutil.which("pandoc"), shutil.which("typst")
    if not pandoc or not typst:
        print("build-manual: pandoc and typst are needed (brew install pandoc typst); skipping", file=sys.stderr)
        return
    md = open(src, encoding="utf-8").read()
    # The H1 becomes the cover; the body starts with a contents page.
    md = re.sub(r"^# .*\n", "", md, count=1)
    base = f'{cfg["product"]}-Manual'

    html_path = os.path.join(out_dir, base + ".html")
    subprocess.run([pandoc, "-f", "gfm", "-t", "html5", "-s", "--metadata", f'title={cfg["product"]} manual', "-o", html_path],
                   input=md, text=True, check=True)
    print("wrote", html_path)

    r = subprocess.run([pandoc, "-f", "gfm", "-t", "typst"], input=md, capture_output=True, text=True)
    if r.returncode != 0:
        print("build-manual: pandoc failed:\n" + r.stderr, file=sys.stderr)
        sys.exit(1)
    body = column_widths(r.stdout, cfg)
    body = keycaps(body)
    # Image paths are relative to the repo root. typst only reads files under
    # its --root, and the output folder may be outside the repo, so compile
    # from the common ancestor and make the image paths absolute from there.
    out_abs = os.path.abspath(out_dir)
    typst_root = os.path.commonpath([root, out_abs])
    prefix = os.path.relpath(root, typst_root).replace(os.sep, "/")
    prefix = "" if prefix == "." else prefix + "/"
    image_root = cfg["image_root"].strip("/")
    body = body.replace(f'image("{image_root}/', f'image("/{prefix}{image_root}/')

    toc = r"""
#set page(..page-settings)
#block(above: 0pt, below: 10pt)[#text(font: body-font, size: 22pt, weight: 700)[Contents]]
#set text(size: 10pt)
#outline(title: none, depth: 2, indent: 4mm)
#pagebreak()
"""
    typ_path = os.path.join(out_dir, base + ".typ")
    with open(typ_path, "w", encoding="utf-8") as f:
        f.write(header(cfg) + cover(cfg, version) + toc + body)
    pdf_path = os.path.join(out_dir, base + ".pdf")
    r = subprocess.run([typst, "compile", "--root", typst_root, typ_path, pdf_path], capture_output=True, text=True)
    if r.returncode != 0:
        print("build-manual: typst failed:\n" + r.stderr, file=sys.stderr)
        sys.exit(1)
    print("wrote", pdf_path)


if __name__ == "__main__":
    main()
