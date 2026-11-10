#!/usr/bin/env python3
"""Lint a Markdown manual before rendering it.

    check-manual.py MANUAL.md [--root REPO] [--controls names.txt]

Fails (exit 1) when:
  - an embedded image does not exist, or has no alt text (the alt text is the caption),
  - a table row has a different number of cells from its header,
  - there is more than one H1 (the cover carries the title; a second H1 duplicates it),
  - a blockquote callout does not start with **Tip:**, **Note:** or **Warning:**.
Prints the set of bold ALL-CAPS names (the ones the renderer turns into
keycaps) so they can be compared with the product's real labels; with
--controls, names not in that file are reported as errors.
"""
import os
import re
import sys

IMG_RE = re.compile(r"!\[([^\]]*)\]\(([^)]+)\)")
KEY_RE = re.compile(r"\*\*((?:[A-Z0-9][A-Z0-9 /\-–]*?)(?: \+ [A-Z0-9][A-Z0-9 /\-–]*?)*)\*\*")


def main():
    args = sys.argv[1:]
    root, controls_path = None, None
    for flag in ("--root", "--controls"):
        if flag in args:
            i = args.index(flag)
            if flag == "--root":
                root = args[i + 1]
            else:
                controls_path = args[i + 1]
            del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    src = args[0]
    root = os.path.abspath(root or os.path.dirname(os.path.abspath(src)))
    lines = open(src, encoding="utf-8").read().split("\n")
    errors = []
    names = set()
    h1 = 0
    header_cells = None
    in_code = False
    for n, line in enumerate(lines, 1):
        if line.startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            continue
        if line.startswith("# "):
            h1 += 1
            if h1 > 1:
                errors.append(f"{n}: second H1; the cover already carries the title")
        for alt, path in IMG_RE.findall(line):
            if not alt.strip():
                errors.append(f"{n}: image {path} has no alt text (it is the caption)")
            if not path.startswith("http") and not os.path.exists(os.path.join(root, path)):
                errors.append(f"{n}: image {path} not found under {root}")
        if line.startswith("|"):
            cells = [c for c in line.strip().strip("|").split("|")]
            if re.fullmatch(r"\|?(\s*:?-+:?\s*\|)+\s*", line.strip() + "|") or set(line.replace("|", "").strip()) <= set("-: "):
                continue  # the separator row
            if header_cells is None:
                header_cells = len(cells)
            elif len(cells) != header_cells:
                errors.append(f"{n}: table row has {len(cells)} cells, header has {header_cells}")
        else:
            header_cells = None
        if line.startswith(">"):
            body = line.lstrip("> ").strip()
            if body and not re.match(r"\*\*(Tip|Note|Warning):\*\*", body):
                errors.append(f"{n}: callout should start with **Tip:**, **Note:** or **Warning:**")
        for m in KEY_RE.findall(line):
            for part in m.split(" + "):
                if re.search(r"[A-Z]", part):
                    names.add(part.strip())
    if controls_path:
        known = {l.strip() for l in open(controls_path, encoding="utf-8") if l.strip()}
        for name in sorted(names - known):
            errors.append(f"keycap {name!r} is not a known control (see {controls_path})")
    print("keycap names:", ", ".join(sorted(names)))
    for e in errors:
        print("check-manual:", e, file=sys.stderr)
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
