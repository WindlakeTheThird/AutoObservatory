# Autoobservatory

A CLI astronomy toolkit that drives a motorized AZ/ALT telescope mount over serial,
plus an ASCII planisphere of the night sky — and a Free Pascal / Lazarus GUI that
does the same thing with a mouse.

Everything is written from scratch: the orbital mechanics, the catalog reader, the
sky projection, the mount control loop, and the regression net that proves all of
it stays correct. Third-party data is credited in [`NOTICE.md`](NOTICE.md).

```
  ./planetarium 45 10                the sky at 45N 10E, right now
  ./planetarium --live --select Vega leave it running, pick a star
  ./autoobservatory 45 10 --from-planetarium -a --serial /dev/ttyUSB0
                                     ...and point the mount at it
```

## Build

Needs `g++` and `make` for the CLI tools.

```bash
make          # autoobservatory, planetarium, calibrate
make check    # the full regression net (both languages, ~10s)
make clean
```

The GUI additionally needs `fpc` and Lazarus LCL (GTK2), and `make check` runs
its tests too:

```bash
make gui          # pas/PasPlanetariumGUI
make gui-run
```

`make check` requires a display, because the GUI tests drive the real binary.
Headless, wrap it: `xvfb-run -a make check`.

## The tools

**`planetarium`** — an ASCII sky dome. Zenith at the centre, horizon as the outer
circle, **N top / E right**. Only the brightest things get letters; the rest are
dots. In `--live` mode it redraws from the real clock and takes keystrokes:
`n`/`p` walk a cursor, `t` locks a target, `f` follows it, `+/-` zooms,
`w/a/s/d` pans, `0` returns to the full sky, `q` quits.

**`autoobservatory`** — resolves an object by name, prints a table, or tracks it
with slew-rate-limited AZ/ALT output to the mount over serial.

**`calibrate`** — runs the mount's homing routine.

The two chain together: pick an object on the dome, and the name lands in
`selected_target.txt` for `autoobservatory --from-planetarium` to read. While a
target is locked, the planetarium shows the mount's last sent position against
the object's live position, with the true angular separation — so a stale link or
a jammed motor is visible rather than silent.

## Accuracy

Measured against JPL Horizons, not assumed. `make check` fails the build if any of
it regresses.

| | |
|---|---|
| Planets | truncated **VSOP87** — 0.15–0.40′ avg, ≤2.1′ worst (Venus at inferior conjunction) |
| Pluto, Ceres, Eris | Keplerian osculating elements — 0.3′, 0.6′, 2.6′ |
| Moon | Schlyter model with perturbation terms — 0.8′ |
| Catalog stars | J2000 precessed to date — **0.002–0.008°** vs astropy apparent |
| Magnitudes | IAU H-G phase function for minor bodies; Schlyter phase for the planets |

A few things that are deliberately *not* modelled: nutation (≤17″), second-order
aberration (<1″), and UT1−UTC (0.2″, measured — it is below the budget and a
delta-T table only makes it worse). See `NOTICE.md` and the vault for the full
error accounting.

## Regression net

This is the part that matters most. Twelve check targets, the ephemeris ones
duplicated across C++ and Pascal so the ports cannot silently diverge:

| Target | What it pins |
|---|---|
| `check-cpp` / `check-gui` | two-epoch ephemeris against real Horizons values. The Pascal table is **generated** from the C++ one (`tools/ephcheck_gen.py`), so the two cannot drift |
| `check-horiz` | the horizontal path (sidereal time → az/alt) — the code that actually aims the mount — against astropy, with a **negative control** that fails if catalog precession is ever dropped |
| `check-hg` | frozen-value table for the IAU H-G phase function, both languages |
| `check-view` | the sky-view projection, including that the default view is bit-identical to the original polar formula |
| `check-vsop` | the two generated 13,379-term VSOP87 tables agree exactly — amplitudes, t-powers, layouts. `check-cpp` cannot see this: its tolerance is 108″ and one term is worth ~0.2″ |
| `check-name` / `check-select` / `check-select-gui` | name resolution: exact, prefix, substring, ambiguous, unknown — across both CLI tools and the GUI |
| `check-cat` | catalog integrity — ranges, uniqueness, Bayer format, self-resolution |
| `check-cli` | the exit-code convention (0 success / 1 runtime / 2 usage) |
| `check-csv` | malformed input: missing files, injected `#` comments, truncated rows, non-numeric fields, CRLF. Asserts exit 1 with a `file:line:row` message, never an abort or a silently degraded sky |

`check-horiz` deserves a note: it exists because the horizontal path was
previously untested, and that gap hid a wrong-sign error in the IAU precession
rotation for months.

CI runs `make check` under Xvfb plus a strict firmware compile, on every push.

## CI

`.github/workflows/ci.yml` runs on every push and PR, as two independent jobs.

**`check`** — installs the toolchain, starts **Xvfb**, and runs the full
`make check`. The display is not optional: `tools/namecheck_gui.sh` skips itself
and still exits 0 when `DISPLAY` is unset, so a run without one reports green
having never exercised the GUI. The workflow therefore polls until the X server
answers *and* fails the job if it doesn't — a fixed sleep raced.

**`firmware`** — installs `arduino-cli` from a **pinned release tarball with a
verified sha256**, then runs `make firmware`, which installs the pinned sketch
libraries and compiles under `-Wall -Wextra -Werror`.

### Reproducing CI locally

```bash
xvfb-run -a make check          # same as CI's check job
ARDUINO_CLI=arduino-cli make firmware
```

`make check` and `make firmware` are the same targets CI runs, so a local
failure and a CI failure have the same cause. Override the CLI with
`ARDUINO_CLI=`; it is resolved with `command -v`, so both a `PATH` name and a
path like `./bin/arduino-cli` work.

### The parts that are easy to get wrong

Everything below exists because it broke at least once:

- **`arduino:renesas_uno` is in Arduino's own package index** — no third-party
  Boards Manager URL. There is no `arduino/setup-arduino` action; it 404s.
- **`LAZDIR` is auto-detected**, not hardcoded — distributions and manual
  installs version that directory differently.
- **Sketch libraries are pinned in `libraries.txt`**, not vendored; they live in
  the Arduino sketchbook, which is not under version control.
- **The only third-party action in the workflow is `actions/checkout`.** The
  firmware job installs its own CLI so it depends on nothing that can vanish.
- **One source of truth for the reference data.** The C++ ephcheck table
  generates the Pascal one; `make check-vsop` proves the two VSOP87 tables
  agree. Neither is checked by hand any more.

## Documentation

The design notes, accuracy tables and post-mortems live in an
[Obsidian vault](ObsMCP/) — start at `ObsMCP/Project Index.md`. Among them:

- [`Autoobservatory Project.md`](ObsMCP/Autoobservatory%20Project.md) — architecture, data files, hard-won lessons
- [`Astronomy Math Reference.md`](ObsMCP/Astronomy%20Math%20Reference.md) — the maths, and the traps
- [`Ephemeris Accuracy Verification.md`](ObsMCP/Ephemeris%20Accuracy%20Verification.md) — how the numbers are proved
- [`Project Review 2026-10.md`](ObsMCP/Project%20Review%202026-10.md) — a recent review, including two real bugs it caught
- [`Mount Follower Firmware.md`](ObsMCP/Mount%20Follower%20Firmware.md) — the serial protocol and calibration routine

## Hardware

Adafruit LSM303AGR (magnetometer + accelerometer) for tilt-compensated heading,
two DC motors on L293D H-bridges, Arduino Uno R4 Minima. Sketch in
`arduino/mount_tracker/`; compiles clean under `-Wall -Wextra -Werror`.

```bash
make firmware        # install pinned libraries, ensure the core, compile
make firmware-libs   # just the sketchbook libraries
```

The sketch's Adafruit libraries are **not vendored** — `arduino-cli` resolves
libraries from the Arduino sketchbook, so a clean machine would otherwise fail
with `Adafruit_LSM303_Accel.h: No such file or directory`. Their versions are
pinned in [`arduino/mount_tracker/libraries.txt`](arduino/mount_tracker/libraries.txt),
which `make firmware` reads. Override the CLI with `ARDUINO_CLI=arduino-cli`.

Because the mount physically moves, the sketch also validates every inbound
target (`sscanf`'s `%f` accepts `nan` and `inf`) and stops driving if no command
arrives for 5 s — a pulled cable or a crashed host must not leave the motors
running. Telemetry keeps flowing past expiry with a `STALE` marker, so the
failure is visible rather than silent.

## License

See [`LICENSE`](LICENSE) for the project's own code and [`NOTICE.md`](NOTICE.md)
for the bundled third-party data.