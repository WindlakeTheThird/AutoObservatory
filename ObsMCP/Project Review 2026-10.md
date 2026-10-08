---
tags:
  - review
  - planning
---
# Project Review 2026-10

Code review of the [[Autoobservatory Project]] (commit state as of 2026-10-08), cross-checked against the vault and against the source. Ranked by impact. `make check` passes clean at time of review — all 9 targets, both languages.

The architecture is sound: the seams ([[sky_view.h]], `object_name.h`, `cli_parse.h`, the CSV→engine function) are real single points of truth, and the regression net is unusually good for a project this size. The findings below are gaps in the guardrails, not in the design.

## 1. Critical — the whole project is uncommitted

```
git log  → fatal: your current branch 'master' does not have any commits yet
git ls-files | wc -l → 0
```

Zero commits, zero tracked files, no `.gitignore`, no remote. Every bug recorded in [[Autoobservatory Project#Hard-won lessons]] — the 26° Moon epoch, Neptune's 3.9°, Saturn's 26′, the `+1e-12` ULP tolerance, the radian-vs-degree H-G bug — is one `rm -rf` from gone.

Fix: `.gitignore` first (build outputs, `bin/`, `graphify-out/`, scratch dirs), then commit. Do this before anything else on this page.

## 2. Biggest technical gap — leap seconds are unmodelled, and they dominate the error budget

`astro_math.h:49` (`GreenwichMeanSiderealTime`) computes GMST from a JD that `JulianDate()` (`astro_math.h:24`) builds directly from UTC calendar fields. That formula assumes **UT1**. UTC has trailed UT1 by **37 s since 2017** (38 s expected in 2027).

37 s × 0.00417807 °/s = **0.155° = 9.3 arcmin** of permanent hour-angle error.

| Term | Size |
|---|---|
| **Unmodelled leap seconds** | **9.3′** |
| Moon (Schlyter) | 0.8′ |
| Planets (VSOP87) | 0.15–0.45′ |
| Mount deadband (`DEADBAND_DEG`) | 0.15° = 9.0′ |

The one unexamined term is **10× the Moon's error and ~20× the planets'**, and it lands exactly on the deadband — the mount stops correcting precisely where the error appears. It is also a constant hour-angle offset, so it does not average out over a long exposure.

It went unnoticed because `make check` is **entirely in equatorial coordinates**. The `GreenwichMeanSiderealTime` → `CalculateHorizontalCoordinates` path — the only function that actually aims the mount — has **no test and no documented accuracy claim anywhere in the vault**.

Fix: apply ΔT = TAI − UT1 to the JD before the GMST call (a small leap-second table, ~15 lines). Then add an ephcheck case in **horizontal** coordinates at a known site so this path has a floor.

## 3. The catalog is not reproducible

`celestial_objects.csv` was, per [[Autoobservatory Project]], "generated from HYG v4.1 and OpenNGC … with a throwaway script." `tools/` holds `vsop87_gen.py` and `ephaudit.py` but **not** the catalog generator. So the 894 rows, the 434 Bayer designations, the genitive naming convention and the 10-column schema are one-time, script-less, and hand-editable from now on. Hand-fix one Bayer typo and the provenance is unrecoverable.

Fix: recover or rewrite the generator and commit it beside the CSVs. A file that cannot be regenerated is one edit away from corruption.

## 4. The dual-language invariant is weaker than documented

[[Autoobservatory Project]] claims the H-G frozen-value test means "the ports cannot diverge without failing one of them." True for `HGPhase()`. **Not** true for the ephemeris itself:

- **`vsop87_trunc.h` (13,378 terms) vs `pas/vsop87_trunc.inc` (13,378 terms)** — same generator, no cross-check. Nothing in `make check` *can* detect a divergence: ephcheck tolerance is 0.03° = 108″, while a single dropped VSOP87 term is ~0.2″. Roughly 500 terms would have to disappear before a test noticed.
- **The 24-row reference tables** in `tools/ephcheck.cpp:29-60` and `pas/ephcheck_pas.lpr:25-48` are hand-transcribed field by field. They agree today (verified by diff). But the whole value of the dual-port check is "both engines read the *same* truth," and nothing enforces that. A one-digit typo in a Dec value makes Pascal and C++ check against different skies while both print `ok`.

Fix: a `check-vsop` target asserting both files declare the same `VSOP_NT` and that the flattened term arrays match exactly, plus one exact-match series evaluation at a fixed epoch (tol ~1e-9, not 0.03°). Generate the Pascal table from the C++ one, same spirit as `vsop87_gen.py`.

## 5. CSV parsing aborts the process — the exit-code convention is a lie for data files

`cli_parse.h:23` wraps `std::stod` in try/catch, so *arguments* degrade cleanly. The *data* side does not:

- `celestial_object_reader.h:56-58` and `planetary_objects.h:567-571` call `std::stod` raw.
- `main.cpp` contains **no `try`/`catch` at all**.

A malformed row → uncaught `std::invalid_argument` → `std::terminate` → **exit 134**. The Makefile's own comment (line 49) says failures "must exit 1 with a message (not abort with 134)", and [[Autoobservatory Project]] documents the CSV as unforgiving of any `#` line. The convention holds for every flag and breaks for every data file.

Inconsistent *within one file*: `planetarium.cpp:404` loads the catalog and exits 1 cleanly; `planetarium.cpp:121` re-reads the same file with `catch (...) {}`, silently discarding the error — and does a **full 894-row parse per frame** whenever the locked target is fainter than the mag limit, with a global catalog already in scope 280 lines up. In `--live` that is 894 rows re-parsed every 10 s for nothing. Hoist to one cached load.

Fix: throw with `file:line` context and wrap each `main` to map that to exit 1. ~15 lines, reusing the machinery already in `cli_parse.h`.

## 6. Smaller, but real

- **`CalculateHorizontalCoordinates` takes 11 positional doubles** (`astro_math.h:71-74`), 6 of them adjacent same-typed date components. `planetarium.cpp:125` passes `..., second, 0.0` for milliseconds. A transposed minute/millisecond compiles clean and costs 600× the error. A `DateTime` struct — or just pass `jd`, the only thing the fields are used for — collapses it to 4 args. This is the one structural refactor worth doing: it also removes a class of impossible-date bugs that `cli_parse.h` currently has to defend against at the parse boundary.
- **No CI.** Nine test targets, all manual, no `.github/`. Worse, `tools/namecheck_gui.sh` skips silently when `DISPLAY` is unset, so a headless CI run would report green having tested nothing. Needs `xvfb-run`, or it should fail loudly.
- **`make check` requires Lazarus and a display** (via `check-select-gui` → `gui`), making the fast loop heavy. Split into `check-core` (no Lazarus) and `check-all`.
- **Doc drift — three live contradictions:**
  1. [[Astronomy Math Reference#Residual budget]] still lists "Mean-elements model floor ~9–17′ (planets)" as current, superseded by [[Ephemeris Accuracy Verification#2026-10 VSOP87 upgrade (arcsecond-class planets)]] (0.15–0.45′).
  2. [[Astronomy Math Reference#Annual aberration]] documents aberration as "finite-differencing over ±0.5 days … first-order covers ~99.5%", which [[Ephemeris Accuracy Verification]] says was wrong and was replaced with a 0.01 d central difference ("the old model's 0.5 d difference applied only half the aberration — fixed").
  3. [[Autoobservatory Project]] describes the 8 planets as VSOP87 in one section and as Horizons osculating elements at 2026-06-09 in another, in the same note.

## Priority

Act on #1 (five minutes, protects everything) and #2 (a real 9 arcmin pointing error, currently invisible to the entire test suite). #3 and #4 are the ones that make the existing test net trustworthy rather than merely present.

## Related

- [[Autoobservatory Project]]
- [[Astronomy Math Reference]]
- [[Ephemeris Accuracy Verification]]
- [[Mount Follower Firmware]]
- [[Project Index]]
