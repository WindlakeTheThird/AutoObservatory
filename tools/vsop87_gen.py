#!/usr/bin/env python3
"""Parse PyMeeus VSOP87 tables (LGPL, https://github.com/architest/pymeeus) and
emit truncated tables as "vsop87_trunc.h" (C++) + "vsop87_trunc.inc" (Pascal).

Terms are kept per table (L/B/R) per power level (L0..L5), dropping all terms
whose amplitude is below LIMIT for that level, i.e. the truncation is
time-safe: the largest dropped contribution at any epoch is bounded by
sum(|dropped amplitudes|) since |t| < 1 for 2000-2030 (|t| ~ 0.03).

Amplitude units: L/B in 1e-8 rad, R in 1e-8 AU. Amplitudes below
LIMIT*1e-8 rad contribute < LIMIT*0.206 arcsec directly (1e-8 rad = 0.00206"),
with L2+ contributions scaled by t^k (<0.001 for our range).

Usage: vsop87_gen.py [LIMIT]   (default LIMIT = 1 -> ~0.2" per-term floor)
Reads: /tmp/<Planet>.py  (PyMeeus Mercury..Neptune)
Writes: vsop87_trunc.h, pas/vsop87_trunc.inc in project root.
"""
import re, sys

PLANETS = ["Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune"]

def parse(path):
    src = open(path).read()
    out = {}
    for name in ("L", "B", "R"):
        m = re.search(r"VSOP87_%s\s*=\s*(\[.*?\n\])\n" % name, src, re.S)
        table = eval(m.group(1))  # trusted local file
        out[name] = table
    return out

def trunc(table, limit):
    keep = []
    for power, terms in enumerate(table):
        kt = [t for t in terms if abs(t[0]) >= limit]
        if kt:
            keep.append((power, kt))
    return keep

def main():
    limit = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    data = {p: parse("/tmp/%s.py" % p) for p in PLANETS}
    kept = dropped = 0
    for p in PLANETS:
        for k in "LBR":
            for _, kt in trunc(data[p][k], limit):
                kept += len(kt)
            dropped += sum(len(t) for t in data[p][k]) - sum(len(kt) for _, kt in trunc(data[p][k], limit))
    print("kept %d, dropped %d (limit %g)" % (kept, dropped, limit))

    # ---------- C++ ----------
    with open("vsop87_trunc.h", "w") as f:
        f.write("// vsop87_trunc.h - truncated VSOP87 heliocentric ecliptic series\n")
        f.write("// (L/B in 1e-8 rad, R in 1e-8 AU) for the 8 major planets, generated\n")
        f.write("// by tools/vsop87_gen.py from the PyMeeus VSOP87 tables (LGPL).\n")
        f.write("// Terms below %g (per-unit t-power) were dropped: max discarded\n" % limit)
        f.write("// contribution ~%g*0.206 arcsec per term for 2000-2030.\n" % limit)
        f.write("#ifndef VSOP87_TRUNC_H\n#define VSOP87_TRUNC_H\n\n#include <cstdint>\n\n")
        f.write("// terms: flat A,B,C array; powers byte array parallel to each series range.\n")
        f.write("struct VsopSeries { unsigned offset, count; const unsigned char* powers; };\n")
        f.write("struct VsopPlanetData { VsopSeries L, B, R; };\n")
        f.write("extern const double VsopTerms[][3]; // A, B, C\n")
        f.write("extern const VsopPlanetData VSOP[8];\n")
        f.write("extern const char* const VSOP_NAMES[8];\n\n#endif\n")
        # single implementation header: emit data too
        f.write("\n#ifdef VSOP87_TRUNC_IMPL\n\n")
        # Build flat term array: for each planet, for L,B,R: blocks of (power, terms)
        flat = []
        pdefs = []
        for p in PLANETS:
            tabs = []
            for k in "LBR":
                tk = trunc(data[p][k], limit)
                off = len(flat)
                pws = []
                for power, terms_ in tk:
                    for A, B, C in terms_:
                        flat.append((A, B, C, power))
                        pws.append(power)
                tabs.append((off, len(flat) - off, pws))
            pdefs.append((p, tabs))
        f.write("extern const unsigned char VSOP_POWERS[];\n")
        f.write("extern const char* const VSOP_NAMES[8];\n\n")
        # terms array
        f.write("const double VsopTerms[%d][3] = {\n" % len(flat))
        for A, B, C, pw in flat:
            f.write("{%.10f,%.10f,%.10f},\n" % (A, B, C))
        f.write("};\n\n")
        # powers arrays (per term)
        pw_arrays = []
        cum = 0
        for i, (p, tabs) in enumerate(pdefs):
            for j, (off, cnt, pws) in enumerate(tabs):
                nm = "PW_%d_%s" % (i, "LBR"[j])
                pw_arrays.append((nm, pws, cum))
                cum += len(pws)
        f.write("const unsigned char VSOP_POWERS[] = {\n")
        allpw = []
        for nm, pws, _ in pw_arrays:
            allpw.extend(pws)
        for i in range(0, len(allpw), 32):
            f.write("".join(str(x) + "," for x in allpw[i:i+32]) + "\n")
        f.write("};\n\n")
        for nm, pws, base in pw_arrays:
            f.write("#define %s (VSOP_POWERS + %d)\n" % (nm, base))
        f.write("\nconst VsopPlanetData VSOP[8] = {\n")
        for i, (p, tabs) in enumerate(pdefs):
            rows = []
            for j, (off, cnt, pws) in enumerate(tabs):
                rows.append("{%u, %u, %s}" % (off, cnt, pw_arrays[(i*3)+j][0]))
            f.write("{%s}, // %s\n" % (", ".join(rows), p))
        f.write("};\n")
        f.write("const char* const VSOP_NAMES[8] = {%s};\n" % ",".join('"%s"' % p for p in PLANETS))
        f.write("\n#endif // VSOP87_TRUNC_IMPL\n")

    # ---------- Pascal ----------
    with open("pas/vsop87_trunc.inc", "w") as f:
        f.write("// vsop87_trunc.inc - truncated VSOP87 series (generated by\n")
        f.write("// tools/vsop87_gen.py from the PyMeeus VSOP87 tables, LGPL).\n")
        f.write("// Terms: A, B, C with A in 1e-8 rad (L/B) or 1e-8 AU (R); per-term power\n")
        f.write("// of t (Julian millennia) in VSOP_POWERS (parallel to each series range).\n\n")
        f.write("const\n  VSOP_NT = %d;\n" % len(flat))
        f.write("  VSOP_TERMS : array[0..VSOP_NT-1, 0..2] of Double = (\n")
        rows = ["(%.10f,%.10f,%.10f)" % t[:3] for t in flat]
        for i in range(0, len(rows), 4):
            sep2 = ",\n" if i + 4 < len(rows) else "\n"
            f.write("    " + ",".join(rows[i:i+4]) + sep2)
        f.write("  );\n")
        allpw = []
        for nm, pws, _ in pw_arrays:
            allpw.extend(pws)
        f.write("  VSOP_POWERS : array[0..%d] of Byte = (\n" % (len(allpw) - 1))
        for i in range(0, len(allpw), 32):
            sep2 = ",\n" if i + 32 < len(allpw) else "\n"
            f.write("    " + ",".join(str(x) for x in allpw[i:i+32]) + sep2)
        f.write("  );\n")
        # layout: for each planet, series L,B,R with (off,cnt,powoff)
        f.write("  VSOP_LAYOUT : array[0..7, 0..2, 0..2] of LongInt = (\n")
        for i, (p, tabs) in enumerate(pdefs):
            rows = []
            for j, (off, cnt, pws) in enumerate(tabs):
                po = sum(len(x[1]) for x in pw_arrays[:(i*3)+j])
                rows.append("(%d,%d,%d)" % (off, cnt, po))
            sep2 = "," if i < len(pdefs) - 1 else ""
            f.write("    (" + ",".join(rows) + ")" + sep2 + " // %s\n" % p)
        f.write("  );\n")
        f.write("  VSOP_NAMES : array[0..7] of string = (%s);\n" % ",".join("'%s'" % p for p in PLANETS))

if __name__ == "__main__":
    main()
