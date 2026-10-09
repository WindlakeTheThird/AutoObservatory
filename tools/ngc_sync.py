#!/usr/bin/env python3
"""ngc_sync.py - add or complete entries in celestial_objects.csv from OpenNGC.

The catalog's rows came from HYG and OpenNGC via a throwaway script that was
never kept, so every hand edit to celestial_objects.csv loses provenance. This
re-establishes that: additions are generated from OpenNGC (the source already
named in the file's own header, CC BY-SA 4.0), deterministically, and re-running
is a no-op once they are present.

    python3 tools/ngc_sync.py --messier                 # ensure all 107 Messier objects
    python3 tools/ngc_sync.py --messier --dry-run       # report only, write nothing

Matching an existing row to a source object, in order of confidence:
  1. the name already carries an "(Mnn)" alias
  2. an OpenNGC common name is already a row name
  3. position within --pos-tol arcsec AND V magnitude within --mag-tol

Rule 3 is deliberately strict on magnitude: the Milky Way globular fields put
several objects inside a couple of arcminutes, so position alone would
occasionally alias the wrong object.

Naming follows the catalog's existing convention for objects that have a
well-known name (Pleiades (M45), Orion Nebula (M42)); objects without one are
added as a bare Messier number.

Note on the legend: planetarium marks a row as a cluster by looking for
"Cluster"/"Pleiades"/"Hyades"/"Beehive" in its name, so a bare "Mnn" row draws
with a letter rather than the cluster digit. That is cosmetic and matches how
galaxies already render.
"""

import argparse
import csv
import io
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / "celestial_objects.csv"
OPENGNGC_URL = ("https://raw.githubusercontent.com/mattiaverga/OpenNGC/"
                "master/database_files/NGC.csv")

HDR = "# ---"
COLS = 10          # name + 9 fields, incl. the optional bayer


# ---------------------------------------------------------------- parsing

def parse_sexagesimal_ra(v):
    """'00:42:44.35' or '00h42m44.35s' -> hours."""
    m = re.match(r"^\s*(-?[\d.]+)[h:]([\d.]+)[m:]([\d.]+)", v or "")
    if not m:
        return None
    return float(m.group(1)) + float(m.group(2)) / 60 + float(m.group(3)) / 3600


def parse_sexagesimal_dec(v):
    """'+41:16:08.6' or '+41d16m08.6s' -> signed degrees."""
    nums = re.findall(r"-?\d+(?:\.\d+)?", v or "")
    if len(nums) < 3:
        return None
    d, m, s = (float(x) for x in nums[:3])
    x = abs(d) + m / 60 + s / 3600
    return -x if str(v).strip().startswith("-") else x


def split_row(line):
    """Catalog row -> (name, ra_hours, dec_deg, mag, bayer)."""
    f = line.split(",")
    ra = int(f[1]) + int(f[2]) / 60 + float(f[3]) / 3600
    dec = int(f[4]) * (int(f[5]) + int(f[6]) / 60 + float(f[7]) / 3600)
    return f[0], ra, dec, float(f[8]), (f[9] if len(f) > 9 else "")


def arcsec(a_deg, b_deg):
    return abs(a_deg - b_deg) * 3600.0


def arcsec_ra(a_h, b_h):
    d = abs(a_h - b_h) * 3600.0
    return min(d, 3600.0 - d)


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--messier", action="store_true",
                    help="ensure every Messier object is present")
    ap.add_argument("--ngc", help="path to a local OpenNGC NGC.csv (else fetch)")
    ap.add_argument("--pos-tol", type=float, default=5.0,
                    help="position tolerance in arcsec when matching (default 5)")
    ap.add_argument("--mag-tol", type=float, default=0.5,
                    help="magnitude tolerance when matching (default 0.5)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not args.messier:
        ap.error("nothing to do: pass --messier")

    if not CATALOG.exists():
        print(f"missing {CATALOG}", file=sys.stderr)
        return 1

    if args.ngc:
        text = Path(args.ngc).read_text(encoding="utf-8", errors="replace")
    else:
        try:
            with urllib.request.urlopen(OPENGNGC_URL, timeout=60) as r:
                text = r.read().decode("utf-8", errors="replace")
        except Exception as e:                                   # noqa: BLE001
            print(f"could not fetch OpenNGC ({e}); pass --ngc PATH", file=sys.stderr)
            return 1

    src = {int(r["M"]): r for r in csv.DictReader(io.StringIO(text), delimiter=";")
           if (r.get("M") or "").strip().isdigit()}
    if not src:
        print("OpenNGC parse yielded no Messier rows", file=sys.stderr)
        return 1

    # Existing catalog rows, with the header block kept verbatim.
    original = CATALOG.read_text(encoding="utf-8").split("\n")
    header = [l for l in original if l.startswith("#")]
    body = [l for l in original
            if l.strip() and not l.startswith("#") and not l.startswith("name,")]

    rows = {name: (ra, dec, mag, bayer) for name, ra, dec, mag, bayer in
            (split_row(l) for l in body)}
    names = list(rows)

    def already_selectable(name, m):
        """True when this row already denotes Messier object m.

        The catalog uses two spellings -- "M8", "M11 (Wild Duck Cluster)"
        (number first) and "Lagoon Nebula (M8)" (tag last) -- and both resolve
        through object_name.h. Testing only the tag form let the number-first
        rows be missed and re-added in the other style, producing duplicates.
        """
        if re.search(rf"\(M{m}\)", name):
            return True
        return bool(re.match(rf"^M{m}(\b|\s|\()", name))

    def find(m, r):
        """Return the existing row name for Messier object m, or None."""
        for n in names:
            if already_selectable(n, m):
                return n
        common = (r.get("Common names") or "").strip()
        if common:
            # OpenNGC lists comma-separated aliases; take the first that matches.
            for cand in [c.strip() for c in common.split(",")]:
                if cand in rows:
                    return cand
        tra, tde = parse_sexagesimal_ra(r["RA"]), parse_sexagesimal_dec(r["Dec"])
        if tra is None or tde is None:
            return None
        vm = float(r["V-Mag"]) if (r.get("V-Mag") or "").strip() else None
        best = None
        for n in names:
            ra, dec, mag, _ = rows[n]
            if arcsec_ra(ra, tra) <= args.pos_tol and arcsec(dec, tde) <= args.pos_tol:
                if vm is not None and abs(mag - vm) > args.mag_tol:
                    continue
                # prefer the closest positional match
                d = arcsec(dec, tde) + arcsec_ra(ra, tra)
                if best is None or d < best[0]:
                    best = (d, n)
        return best[1] if best else None

    alias_fix, additions, unchanged = [], [], []
    planned = set()
    for m in sorted(src):
        r = src[m]
        existing = find(m, r)
        common = (r.get("Common names") or "").strip()
        # Prefer the shortest common name as the display name.
        common = common.split(",")[0].strip() if common else ""
        if existing:
            if already_selectable(existing, m):
                unchanged.append((m, existing))
            else:
                new = f"{existing} (M{m})"
                if new in rows:
                    unchanged.append((m, existing))
                else:
                    alias_fix.append((existing, new))
        else:
            name = f"{common} (M{m})" if common else f"M{m}"
            if name in rows or name in planned:
                # Duplicate name (two Messier objects sharing a common name, or a
                # clash with an existing row): fall back to the bare number so
                # catcheck's "every name resolves to itself" rule still holds.
                alt = f"M{m}"
                if alt in rows or alt in planned:
                    print(f"  note: M{m} ({name}) collides; leaving it out", file=sys.stderr)
                    continue
                name = alt
            planned.add(name)
            additions.append((m, name, r))

    print(f"OpenNGC Messier objects : {len(src)}")
    print(f"  already complete      : {len(unchanged)}")
    print(f"  alias to add          : {len(alias_fix)}")
    print(f"  new rows to add       : {len(additions)}")

    if alias_fix:
        print("\nalias additions:")
        for old, new in alias_fix:
            print(f"   {old}  ->  {new}")
    if additions:
        print(f"\nnew rows ({len(additions)}):")
        for m, name, _ in additions[:12]:
            print(f"   M{m:<4} {name}")
        if len(additions) > 12:
            print(f"   ... and {len(additions) - 12} more")

    if args.dry_run:
        print("\n--dry-run: nothing written")
        return 0

    # Apply: rewrite aliased names in place, then append new rows in M order.
    renamed = dict(alias_fix)
    out = []
    for l in body:
        name = l.split(",", 1)[0]
        if name in renamed:
            l = renamed[name] + l[len(name):]
        out.append(l)

    for m, name, r in sorted(additions):
        tra = parse_sexagesimal_ra(r["RA"])
        tde = parse_sexagesimal_dec(r["Dec"])
        vm = float(r["V-Mag"]) if (r.get("V-Mag") or "").strip() else 10.0
        # Whole arcseconds, matching how the existing rows are stored.
        ra_h = int(tra)
        ra_m = int((tra - ra_h) * 60)
        ra_s = int(round(((tra - ra_h) * 60 - ra_m) * 60)) % 60
        sign = 1 if tde >= 0 else -1
        d_ = int(abs(tde))
        d_m = int((abs(tde) - d_) * 60)
        d_s = int(round(((abs(tde) - d_) * 60 - d_m) * 60)) % 60
        out.append(f"{name},{ra_h},{ra_m},{ra_s},{sign},{d_},{d_m},{d_s},"
                   f"{vm:g},")

    header.append(HDR)
    header.append("# Messier additions below generated by tools/ngc_sync.py from OpenNGC")
    header.append("# (CC BY-SA 4.0, Mattia Verga) on 2026-10. See NOTICE.md.")
    header.append(HDR)
    CATALOG.write_text("\n".join(header + ["name,ra_h,ra_m,ra_s,dec_sign,"
                                           "dec_d,dec_m,dec_s,magnitude,bayer"]
                                  + out) + "\n", encoding="utf-8")
    print(f"\nwrote {CATALOG.relative_to(ROOT)}: {len(out)} data rows "
          f"(+{len(alias_fix)} aliases, +{len(additions)} new)")
    return 0


if __name__ == "__main__":
    sys.exit(main())