---
tags: [review, planning]
---

# Project Review 2026-10

Code review of the [[Autoobservatory Project]] on 2026-10-08, cross-checked against the vault and the source. Ranked by impact.

**Status after this session:** #1 done (git initialised, 55 files committed, pushed), #2 done (and it turned out to be hiding a much larger bug), #3 done (CI added). #4 and #5 remain open. #2 and #6(c) were investigated and closed as non-issues, with the measurements recorded below.

The architecture is sound: the seams (`sky_view.h`, `object_name.h`, `cli_parse.h`, the CSV→engine function) are real single points of truth, and the regression net is unusually good for a project this size. The findings below are gaps in the guardrails, not in the design.

## 1. Critical — the whole project was uncommitted — RESOLVED

```
git log  → fatal: your current branch 'master' does not have any commits yet
git ls-files | wc -l → 0
```

Zero commits, zero tracked files, no `.gitignore`, no remote. Every bug recorded in [[Autoobservatory Project#Hard-won lessons]] — the 26° Moon epoch, Neptune's 3.9°, Saturn's 26′, the `+1e-12` ULP tolerance, the radian-vs-degree H-G bug — was one `rm -rf` from gone.

Fixed: `.gitignore` added (build output, `bin/`, `graphify-out/`, scratch dirs), initial commit `8b54c00`, pushed to `origin/main`. Verified self-sufficient by `make clean && make`.

## 2. The horizontal path was untested — and that was hiding two real bugs

Every other check in `make check` works in **equatorial** coordinates. `GreenwichMeanSiderealTime` → `CalculateHorizontalCoordinates` — the only code that actually aims the mount — had **no test and no documented accuracy claim anywhere in the vault**. Adding that test found the two defects below.

### 2a. UT1−UTC: measured, and deliberately NOT applied

> **Correction.** An earlier draft of this note claimed unmodelled leap seconds caused a **9.3 arcmin** pointing error. That was wrong by ~20×. The correction that matters for GMST is **UT1 − UTC**, not TAI − UTC: `TAI − UT1` is no longer the fixed 32.184 s it was defined as, because Earth has slowed and it has grown past 37 s, absorbing nearly all 37 leap seconds.

Measured against astropy 8.0.1 at 2026-09-23 21:00 UTC: the genuine UT1−UTC effect moves GMST by **−0.20 arcsec**, and the project's GMST polynomial already sits within **+0.06 arcsec** of the IAU reference. An IERS ΔT table was implemented, measured, and **reverted** — it made the result worse (−1.24″) while adding a table that goes stale. `Ut1MinusUtcSeconds()` survives in `astro_math.h` as a documented zero-returning stub so the question is not re-litigated. Negligible against the budget (planets 9–27″, Moon 48″).

### 2b. The real bugs: two precession defects

**(i) Catalog stars were never precessed.** `celestial_objects.csv` rows are J2000 but were fed straight into `CalculateHorizontalCoordinates`. The dome and the mount were **9–21 arcmin** off — larger than the firmware `DEADBAND_DEG` (0.15° = 9′). Fixed by adding `PrecessRaDecJ2000ToDate` (`planetary_objects.h`, mirrored `astrop.pas`) and calling it at every *catalog* site: `planetarium.cpp` (star loop, tracking-panel fallback) and `main.cpp` (table loop, `findObject`, and the Moon-occultation check, which compared an of-date Moon against a J2000 star). The Moon, Sun and planets are already mean-of-date and must **not** be precessed — that is the ~20′ Moon double-precession trap in reverse.

**(ii) `PrecessJ2000ToDate` had the wrong sign on θ.** The IAU 2006 matrix uses R2(**−θ**); the code implemented R2(+θ), i.e. the transpose, giving a **2θ ≈ 1071″** rotation error. Verified element-by-element against `erfa.pmat06` — after the sign fix all nine matrix elements agree.

Bug (ii) had been silently corrupting the **Keplerian** path (Pluto, Ceres, asteroids, TNOs) — the only consumers of that function, since the 8 planets run on already-of-date VSOP87 series. Against real JPL Horizons:

| Body | Before | After |
|---|---|---|
| Pluto | 12.01′ | **0.30′** |
| Ceres | 8.65′ | **0.59′** |
| Eris | 15.25′ | **2.58′** |
| Haumea | 15.84′ | **5.63′** |
| Makemake | 18.10′ | **4.99′** |
| **Mean** | **14.36′** | **3.02′** |

The documented "Pluto ~11′" and "asteroids/TNOs 0.2–0.3°" residuals were largely this bug, not "linear propagation of osculating elements" — see [[Ephemeris Accuracy Verification]].

The old ephcheck values for these bodies were *model extrapolations*, so they agreed with the buggy model and could not arbitrate. Replaced with real Horizons geocentric apparent values; tolerances tightened from 0.5–0.6° to 0.15°.

### The guard that now exists

`tools/horzcheck.cpp` + `pas/horzcheck_pas.lpr` (`make check-horiz`) feed J2000 catalog coordinates through precess + horizontal and compare against astropy apparent place. Tolerance 0.05° (nutation ≤17″ + aberration ≤20.5″ are unmodelled by design). It also runs a **negative control** — the same objects with precession skipped must FAIL — so the tolerance can never be widened into meaninglessness. Current pass: **0.002–0.008°**, and the two language ports agree to 4 decimals.

## 3. No CI — RESOLVED

`.github/workflows/ci.yml` runs on every push and PR: one job installs g++/fpc/Lazarus/GTK2, starts **Xvfb**, and runs the full `make check`; a second compiles the firmware with `-Wall -Wextra -Werror`.

The Xvfb step exists because `tools/namecheck_gui.sh` **skips itself and exits 0 when `DISPLAY` is unset** — a headless run would otherwise report green having tested nothing. The workflow now asserts the display is actually up and fails loudly if it is not.

Still open: `make check` requires Lazarus and a display, which makes the local fast loop heavy. Worth splitting into `check-core` (no Lazarus) and `check-all`.

## 4. The catalog is not reproducible — ACCEPTED as out of scope

`celestial_objects.csv` was "generated from HYG v4.1 and OpenNGC … with a throwaway script" that is not in the repo. Decision (2026-10-08): the catalog is *transformed from external data* and the generator is gone; it is now committed and pushed, so it is safe. Accepting that hand-edits lose provenance. A future change to the object list should regenerate from HYG/OpenNGC afresh rather than patch the CSV in place.

## 5. The dual-language invariant is weaker than documented — OPEN

[[Autoobservatory Project]] claims the H-G frozen-value test means "the ports cannot diverge without failing one of them." True for `HGPhase()`. **Not** true for the ephemeris itself:

- **`vsop87_trunc.h` (13,378 terms) vs `pas/vsop87_trunc.inc` (13,378 terms)** — same generator, no cross-check. Nothing in `make check` *can* detect a divergence: ephcheck tolerance is 0.03° = 108″, a single dropped VSOP87 term is ~0.2″. Roughly 500 terms would have to disappear before a test noticed.
- **The 24-row reference tables** in `tools/ephcheck.cpp` and `pas/ephcheck_pas.lpr` are hand-transcribed field by field. They were verified identical during this review, but nothing enforces that. A one-digit typo in a Dec value makes Pascal and C++ check against different skies while both print `ok`. *(Partially addressed: the minor-body rows are now real Horizons values with 4× tighter tolerances, so a typo now fails loudly instead of passing. The planets and Moon rows are still hand-duplicated.)*

Fix: a `check-vsop` target asserting both files declare the same `VSOP_NT` and that the flattened term arrays match exactly, plus one exact-match series evaluation at a fixed epoch (tol ~1e-9, not 0.03°). Generate the Pascal table from the C++ one, same spirit as `vsop87_gen.py`.

## 6. CSV parsing aborts the process — OPEN

`cli_parse.h:23` wraps `std::stod` in try/catch, so *arguments* degrade cleanly. The *data* side does not:

- `celestial_object_reader.h:56-58` and `planetary_objects.h:567-571` call `std::stod` raw.
- `main.cpp` contains **no `try`/`catch` at all**.

A malformed row → uncaught `std::invalid_argument` → `std::terminate` → **exit 134**. The Makefile's own comment (line 49) says failures "must exit 1 with a message (not abort with 134)", and [[Autoobservatory Project]] documents the CSV as unforgiving of any `#` line. The convention holds for every flag and breaks for every data file.

Inconsistent *within one file*: `planetarium.cpp:404` loads the catalog and exits 1 cleanly; `planetarium.cpp:121` re-reads the same file with `catch (...) {}`, silently discarding the error — and does a **full 894-row parse per frame** whenever the locked target is fainter than the mag limit, with a global catalog already in scope 280 lines up. In `--live` that is 894 rows re-parsed every 10 s for nothing. Hoist to one cached load.

Fix: throw with `file:line` context and wrap each `main` to map that to exit 1. ~15 lines, reusing the machinery already in `cli_parse.h`.

## 7. Smaller, but real

- **`CalculateHorizontalCoordinates` takes 11 positional doubles** (`astro_math.h:71-74`), 6 of them adjacent same-typed date components. `planetarium.cpp` passes `..., second, 0.0` for milliseconds. A transposed minute/millisecond compiles clean and costs 600× the error. A `DateTime` struct — or just pass `jd`, the only thing the fields are used for — collapses it to 4 args. This is the one structural refactor worth doing: it also removes a class of impossible-date bugs that `cli_parse.h` currently defends against at the parse boundary.
- **Doc drift — three live contradictions:**
  1. [[Astronomy Math Reference#Residual budget]] still lists "Mean-elements model floor ~9–17′ (planets)" as current, superseded by the VSOP87 upgrade (0.15–0.45′).
  2. [[Astronomy Math Reference#Annual aberration]] documents aberration as "finite-differencing over ±0.5 days … first-order covers ~99.5%", which [[Ephemeris Accuracy Verification]] says was wrong and was replaced with a 0.01 d central difference.
  3. [[Autoobservatory Project]] describes the 8 planets as VSOP87 in one section and as Horizons osculating elements at 2026-06-09 in another, in the same note.
- **A brittle test was found and fixed while doing #2.** `tools/clicheck.sh` asserted that follow mode centred Vega at hardcoded `289.2, 39.0`. Any ephemeris or epoch-handling change broke it, and a stale constant is indistinguishable from a real regression. Replaced with `expect_view_follows`, which asserts the view centre equals the target's own az/alt *from the same output* — self-consistent and time-independent. (Parsing must target the legend line: the header's `tracking:` line prints the same numbers at 2-decimal precision, so a naive first-match compares 289.2 against 289.15 and always fails.)

## Priority

#4 and #5 are what make the existing test net trustworthy rather than merely present — both are about *silent divergence* rather than wrong answers. #6 is a robustness gap that contradicts a documented contract. #7's `DateTime` refactor is optional.

## Related

- [[Autoobservatory Project]]
- [[Astronomy Math Reference]]
- [[Ephemeris Accuracy Verification]]
- [[Mount Follower Firmware]]
- [[Pascal Planetarium GUI]]
- [[Project Index]]