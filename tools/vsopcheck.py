#!/usr/bin/env python3
"""vsopcheck.py - cross-check the two generated VSOP87 tables.

The 8 major planets are evaluated from truncated VSOP87 series that are emitted
TWICE: `vsop87_trunc.h` (C++) and `pas/vsop87_trunc.inc` (Free Pascal). Both
come from tools/vsop87_gen.py, but nothing verified that they stayed in sync.

That gap is not hypothetical and `make check` structurally cannot catch it: the
ephemeris tolerance is 0.03 deg (108 arcsec), while a single dropped VSOP87
term is worth about 0.2 arcsec. Roughly 500 terms would have to vanish from one
file before ephcheck noticed.

This parses both files independently and asserts they encode identical data:
term count, per-series (offset, count) layout, every A/B/C amplitude, every
t-power byte, and the planet names. A regeneration that updated only one file,
or a bad merge that dropped a block, fails here immediately.

Run via `make check-vsop`. Exit 0 on agreement, 1 on any divergence.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CPP = ROOT / "vsop87_trunc.h"
PAS = ROOT / "pas" / "vsop87_trunc.inc"

NUM = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"


def _numbers(text):
    return [float(m) for m in re.findall(NUM, text)]


def _block(text, start_marker):
    """Return the text between start_marker and its matching closing brace/paren."""
    i = text.index(start_marker) + len(start_marker)
    opener = text[i - len(start_marker) + len(start_marker) - 1] if False else None
    # find the first '{' or '(' after the marker
    j = min(k for k in (text.find("{", i), text.find("(", i)) if k != -1)
    open_ch, close_ch = ("{", "}") if text[j] == "{" else ("(", ")")
    depth = 0
    for k in range(j, len(text)):
        if text[k] == open_ch:
            depth += 1
        elif text[k] == close_ch:
            depth -= 1
            if depth == 0:
                return text[j + 1 : k]
    raise ValueError(f"unterminated block after {start_marker!r}")


def parse_cpp(text):
    """Parse vsop87_trunc.h into (terms, powers, layout, names)."""
    n_decl = re.search(r"VsopTerms\[(\d+)\]\[3\]", text)
    if not n_decl:
        raise ValueError("VsopTerms declaration not found in vsop87_trunc.h")
    n_terms = int(n_decl.group(1))
    terms = _numbers(_block(text, f"VsopTerms[{n_decl.group(1)}][3] = "))
    powers = [int(v) for v in _numbers(_block(text, "VSOP_POWERS[] = "))]

    # Resolve the PW_* power-block offsets from the #defines, so the layout table
    # compares directly against Pascal's explicit third column.
    pw = {m.group(1): int(m.group(2))
          for m in re.finditer(r"#define\s+(PW_\w+)\s+\(VSOP_POWERS \+ (\d+)\)", text)}
    layout = []
    for m in re.finditer(
            r"\{\{(\d+),\s*(\d+),\s*(PW_\d+_[LBR])\},\s*"
            r"\{(\d+),\s*(\d+),\s*(PW_\d+_[LBR])\},\s*"
            r"\{(\d+),\s*(\d+),\s*(PW_\d+_[LBR])\}\}", text):
        layout.append([(int(m.group(1)), int(m.group(2)), pw[m.group(3)]),
                       (int(m.group(4)), int(m.group(5)), pw[m.group(6)]),
                       (int(m.group(7)), int(m.group(8)), pw[m.group(9)])])
    names = re.findall(r'VSOP_NAMES\[8\] = \{(.*?)\};', text, re.S)
    names = re.findall(r'"([^"]+)"', names[0]) if names else []
    return n_terms, terms, powers, layout, names


def parse_pascal(text):
    """Parse pas/vsop87_trunc.inc into (terms, powers, layout, names)."""
    n_decl = re.search(r"VSOP_NT\s*=\s*(\d+)", text)
    if not n_decl:
        raise ValueError("VSOP_NT not found in vsop87_trunc.inc")
    n_terms = int(n_decl.group(1))
    terms = _numbers(_block(text, "of Double = "))          # first Double block = TERMS
    powers = [int(v) for v in _numbers(_block(text, "of Byte = "))]

    lay = _block(text, "of LongInt = ")
    layout = []
    for row in re.finditer(r"\(\((\d+),(\d+),(\d+)\),\s*\((\d+),(\d+),(\d+)\),\s*"
                           r"\((\d+),(\d+),(\d+)\)\)", lay):
        g = [int(v) for v in row.groups()]
        layout.append([(g[0], g[1], g[2]), (g[3], g[4], g[5]), (g[6], g[7], g[8])])
    names = re.findall(r"VSOP_NAMES[^=]*=\s*\((.*?)\);", text, re.S)
    names = re.findall(r"'([^']+)'", names[0]) if names else []
    return n_terms, terms, powers, layout, names


def main():
    problems = []

    if not CPP.exists():
        print(f"FAIL missing {CPP}")
        return 1
    if not PAS.exists():
        print(f"FAIL missing {PAS}")
        return 1

    n_c, t_c, p_c, l_c, n_cn = parse_cpp(CPP.read_text(encoding="utf-8", errors="replace"))
    n_p, t_p, p_p, l_p, n_pn = parse_pascal(PAS.read_text(encoding="utf-8", errors="replace"))

    print("== VSOP87 table cross-check (C++ vs Pascal) ==")

    if n_c != n_p:
        problems.append(f"declared term count differs: C++ {n_c}, Pascal {n_p}")
    else:
        print(f"ok   declared term count matches ({n_c})")

    # Amplitudes. Compare as a flat stream so a term moved between rows is still
    # caught; exact equality is the right bar because both are printed from the
    # same generator with the same formatting.
    if len(t_c) != len(t_p):
        problems.append(f"term value count differs: C++ {len(t_c)}, Pascal {len(t_p)}")
    elif t_c != t_p:
        bad = [i for i, (a, b) in enumerate(zip(t_c, t_p)) if a != b]
        problems.append(
            f"{len(bad)} term value(s) differ; first at flat index {bad[0]}: "
            f"C++ {t_c[bad[0]]} vs Pascal {t_p[bad[0]]}")
    else:
        print(f"ok   all {len(t_c)} term amplitudes identical")

    if len(p_c) != len(p_p):
        problems.append(f"power byte count differs: C++ {len(p_c)}, Pascal {len(p_p)}")
    elif p_c != p_p:
        bad = [i for i, (a, b) in enumerate(zip(p_c, p_p)) if a != b]
        problems.append(
            f"{len(bad)} t-power byte(s) differ; first at index {bad[0]}: "
            f"C++ {p_c[bad[0]]} vs Pascal {p_p[bad[0]]}")
    else:
        print(f"ok   all {len(p_c)} t-power bytes identical")

    if l_c != l_p:
        if len(l_c) != len(l_p):
            problems.append(f"layout row count differs: C++ {len(l_c)}, Pascal {len(l_p)}")
        else:
            for i, (a, b) in enumerate(zip(l_c, l_p)):
                if a != b:
                    problems.append(f"layout row {i} differs: C++ {a} vs Pascal {b}")
                    break
    else:
        print(f"ok   all {len(l_c)} series layouts (offset, count, power base) identical")

    if n_cn != n_pn:
        problems.append(f"planet names differ: C++ {n_cn} vs Pascal {n_pn}")
    else:
        print(f"ok   planet names identical ({', '.join(n_cn)})")

    # Sanity: the last series must end exactly at the term count. A truncated
    # file usually shows up here even if the two agree with each other.
    if l_c:
        last = max(off + cnt for series in l_c for off, cnt, _ in series)
        if last != n_c:
            problems.append(f"layout covers {last} terms but {n_c} are declared "
                            f"(file is internally inconsistent)")
        else:
            print(f"ok   layout covers exactly {last} terms (no gap, no overrun)")

    if problems:
        print()
        for p in problems:
            print(f"FAIL {p}")
        print(f"\n{len(problems)} VSOP87 cross-check problem(s). The two tables "
              f"have DIVERGED -- ephcheck will not notice (0.03 deg tolerance vs "
              f"~0.2 arcsec per term).")
        return 1

    print("\nVSOP87 tables agree exactly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())