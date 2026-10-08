#!/usr/bin/env python3
"""Fetch JPL Horizons apparent RA/Dec (geocenter, 500) for the 8 planets,
monthly 2024-01..2027-12, compare against model dump, report per-planet error stats."""
import math, sys, time, urllib.parse, urllib.request

BODIES = ["Mercury","Venus","Mars","Jupiter","Saturn","Uranus","Neptune","Pluto"]
ID = {"Mercury":"199","Venus":"299","Mars":"499","Jupiter":"599","Saturn":"699",
      "Uranus":"799","Neptune":"899","Pluto":"999"}

def fetch(body):
    url = ("https://ssd.jpl.nasa.gov/api/horizons.api?format=text&COMMAND='" + ID[body] + "'"
           "&OBJ_DATA='NO'&MAKE_EPHEM='YES'&EPHEM_TYPE='OBSERVER'&CENTER='500@399'"
           "&START_TIME='2024-01-01'&STOP_TIME='2027-12-01'&STEP_SIZE='1mo'"
           "&QUANTITIES='2'&ANG_FORMAT='DEG'&APPARENT='AIRLESS'&REF_SYSTEM='ICRF'"
           "&CAL_FORMAT='CAL'&TIME_TYPE='UT'&CSV_FORMAT='YES'")
    with urllib.request.urlopen(url, timeout=60) as r:
        text = r.read().decode()
    lines = text.splitlines()
    try:
        i = next(k for k, l in enumerate(lines) if l.startswith("$$SOE"))
        j = next(k for k, l in enumerate(lines) if l.startswith("$$EOE"))
    except StopIteration:
        print("no ephemeris for", body, file=sys.stderr); return {}
    out = {}
    for l in lines[i+1:j]:
        p = l.split(",")
        # quantities=2 apparent RA/DEC: JDTDB, Date, , , RA(deg), DEC(deg),
        # line: " 2024-Jan-01 00:00, , ,   RA,   DEC,"
        raw = p[0].strip()
        y, mon = raw[:4], raw[5:8]
        dd = raw[9:11]
        date = "{}-{:02d}-{}".format(y, ['Jan','Feb','Mar','Apr','May','Jun',
            'Jul','Aug','Sep','Oct','Nov','Dec'].index(mon)+1, dd)
        ra = float(p[3]); dec = float(p[4])
        out[date] = (ra, dec)
    return out

def angular_sep(ra1, d1, ra2, d2):
    r1, r2 = math.radians(ra1), math.radians(ra2)
    dd = math.radians(d2 - d1); dr = r2 - r1
    a = math.sin(dd/2)**2 + math.cos(r1)*math.cos(r2)*math.sin(dr/2)**2
    return 2*math.degrees(math.asin(min(1.0, math.sqrt(a))))

def main():
    model = {}
    with open(".model.txt") as f:
        for l in f:
            d, n, ra, dec = l.split()
            model[(d, n)] = (float(ra)*15.0, float(dec))
    stats = {b: [] for b in BODIES}
    for b in BODIES:
        try:
            ref = fetch(b)
        except Exception as e:
            print("fetch failed", b, e); continue
        time.sleep(1)
        for (d, n), (mra, mdec) in sorted(model.items()):
            if n != b: continue
            if d not in ref:
                print("missing ref", b, d, file=sys.stderr); continue
            rra, rdec = ref[d]
            stats[b].append((d, angular_sep(mra, mdec, rra, rdec)))
    worst = 0.0
    for b in BODIES:
        if not stats[b]:
            print(b, "NO DATA"); continue
        errs = [e for _, e in stats[b]]
        mx = max(errs); avg = sum(errs)/len(errs)
        worst = max(worst, mx)
        wd = max(stats[b], key=lambda t: t[1])[0]
        print("%-8s n=%2d  avg %7.2f'  max %7.2f'  worst %s" %
              (b, len(errs), avg*60, mx*60, wd))
        for d, e in stats[b]:
            if e*60 > 20:
                print("    %s %8.2f'" % (d, e*60))

if __name__ == "__main__":
    main()
