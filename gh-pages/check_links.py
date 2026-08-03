#!/usr/bin/env python3
"""Fail the build if the site links to something it does not contain.

Every internal href in the built site must resolve to a file that exists. The two ways a doc
can legitimately point outside the site -- an absolute URL, and a repo file rewritten to a
GitHub blob URL by build_site.py -- are both absolute and therefore skipped here.

This exists because the failure it catches is silent: a doc is renamed or a section removed,
the site still builds, and the only symptom is a 404 that nobody clicks until much later.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent / "public"


def main() -> int:
    if not ROOT.is_dir():
        print(f"no built site at {ROOT} -- run build_site.py first", file=sys.stderr)
        return 2

    broken: list[tuple[str, str]] = []
    pages = sorted(ROOT.rglob("*.html"))
    for page in pages:
        html = page.read_text(encoding="utf-8")
        for href in re.findall(r'(?:href|src)="([^"]+)"', html):
            if "://" in href or href.startswith(("#", "mailto:", "data:")):
                continue
            target = href.split("#")[0]
            if not target:
                continue
            resolved = (page.parent / target).resolve()
            if not resolved.exists():
                broken.append((str(page.relative_to(ROOT)), href))

    if broken:
        print(f"{len(broken)} broken link(s) in the built site:", file=sys.stderr)
        for page, href in broken:
            print(f"  {page}: {href}", file=sys.stderr)
        return 1

    print(f"link check: {len(pages)} pages, no broken internal links")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
