# Pascal Planetarium GUI

Form application version of the [[Using The Planetarium|ASCII planetarium]], written in Free Pascal / Lazarus (LCL). Lives in `pas/` inside the [[Autoobservatory Project]] checkout. Uses the same `celestial_objects.csv` / `planetary_objects.csv` data files — run it from the project root.

## Build & run

```bash
make gui          # builds pas/PasPlanetariumGUI via fpc (LCL search paths from LAZDIR, default /usr/lib/lazarus/3.0)
make gui-run      # build + launch
make check-gui    # unit-check the Pascal astro math against the C++ ephcheck reference values
make check-select-gui  # drive --select with exact/fuzzy/ambiguous/unknown names plus strict --date/--time cases (skipped when DISPLAY is unset)
make clean        # also removes Pascal build artifacts
```

Requires `fpc` + Lazarus LCL (GTK2 on Linux). `pas/PlanetariumGUI.lpi` also opens directly in the Lazarus IDE.

> **Harmless GTK message.** Launching prints `Gtk-Message: Failed to load module "canberra-gtk-module"` when the GTK2 event-sound module isn't installed — LCL links GTK2, and many systems ship only `libcanberra-gtk3-module`. It only disables widget event sounds; the display and tracking are unaffected. Install `libcanberra-gtk-module` to silence it.

## Command line

```bash
./pas/PasPlanetariumGUI                       # live sky at default 45N 10E
./pas/PasPlanetariumGUI 51.5 -0.1             # live sky at London
./pas/PasPlanetariumGUI --date 2026-10-15 --time 21:00        # replay a fixed instant (UTC)
./pas/PasPlanetariumGUI 45 10 --date 2026-10-15 --time 21:30  # fixed night + location
./pas/PasPlanetariumGUI --select Vega         # set the mount target non-interactively
./pas/PasPlanetariumGUI 45 10 --fov 30                       # a 30-deg field centred on the zenith
./pas/PasPlanetariumGUI --fov 24 --center 289.2,39.0         # a 24-deg field around Vega
./pas/PasPlanetariumGUI --fov 24 --select Vega --follow      # ... and follow Vega as the sky turns
./pas/PasPlanetariumGUI --window 90,180,10,60                # only az 90-180, alt 10-60 (rect window)
```

- Positional `lat lon` as in the C++ tool (south/west negative)
- `--date YYYY-MM-DD` and `--time HH:MM[:SS]` are UTC; using either freezes time — the window title shows **FIXED TIME**. Parsing is strict and shared with the C++ tools via `astrop.pas` (`ParseDateArg`/`ParseTimeArg`, the counterparts of `cli_parse.h`): malformed or impossible values (Feb 30, hour 25, wrong separators) are usage errors — same message text, exit 2; leap days are accepted
- `--select NAME` is case-insensitive and resolves to the canonical catalog spelling — exact name first, then a unique prefix or substring (`arctur` → Arcturus) — selects the object on the dome and writes `selected_target.txt`. Pair it with `--date`/`--time` to pre-aim a future session, or drive it from a script. An ambiguous query lists the matching objects and exits 1; a name that matches nothing prints the catalogs it searched and exits 1
- Without them the display follows the real clock (live mode, 1 s refresh)
- `--fov DEG` shows a **limited view** — a circular field of that angular diameter (must be smaller than a full sky, `0 < fov < 180`; omit for the whole sky) — and `--center AZ,ALT` aims it (az `[0,360)`, alt `[-90,90]`, or two tokens `AZ ALT`). Invalid values are usage errors (exit 2) with the same wording as the terminal planetarium.  The projection (azimuthal equidistant about the centre) is shared with the C++ tool through `astrop.pas`, so both maps agree
- `--follow` keeps the limited view centred on the `--select` target, re-centring on the object's live azimuth/altitude every frame (press `f` to toggle it interactively; it does nothing without a target). Panning with `w`/`a`/`s`/`d` or resetting with `0` is a manual override and turns follow off, while `+`/`-` zooming keeps tracking
- `--window AZ1,AZ2,ALT1,ALT2` (or press `x` to toggle it on/off) switches to the **rectangular az/alt window** mode: only objects whose azimuth lies between AZ1 and AZ2 (shortest arc, so `350,20` straddles north) and whose altitude lies between ALT1 and ALT2 are drawn and listed. The dome is framed by the window's straight edges instead of the field circle, and the header's `view:` line shows the window bounds. Malformed or out-of-range values (fewer than four numbers, ALT1 >= ALT2, azimuth/altitude outside [0,360)/[-90,90]) are usage errors (exit 2). In this mode `w`/`a`/`s`/`d` slide the whole window by 5 degrees and `0` drops back to the full sky

## On-screen controls

- **Trackbar (bottom)** — magnitude limit −1 … 10 in 0.5 steps, updates the map immediately
- **View menu → Legend column** (or press `L`) — collapses the right-hand legend so the dome uses the full window width; the menu item carries a checkmark reflecting the state
- **Keyboard** — the same keys as the terminal planetarium: `n`/`p` (or Tab) walk a target cursor (yellow ring, `>` in the legend), `t` or Enter confirms it as the mount target (red ring, `*`), `c`/Esc clears. `L` toggles the legend column so the dome re-centers across the full window width. `+`/`-` zoom the field in/out, `w`/`a`/`s`/`d` pan the centre (`w` = toward the zenith), `f` follows the locked selection as the sky turns, `x` toggles the rectangular az/alt window, `0` returns to the full sky. Panning stops follow. The lime status line tracks whichever mode you are in
- **Mouse-over** — any object marker or legend line shows a tooltip: name, az, alt, mag; the hovered object gets a cyan ring
- **Click** — a marker or legend line selects that object (persistent red ring) **and writes its name to `selected_target.txt`**, the same hand-off file the terminal planetarium uses. The lime status line under the map then shows the ready-to-run command, e.g. `target: Vega  ->  ./autoobservatory 45.0000 10.0000 --from-planetarium -a`. Clicking empty sky clears the selection and the file.
- Selection and tooltips also work with `--date`/`--time` replays, so you can pick a target for a future night

## Reading the display

- **Layout**: a text band across the top (header + tracking lines), the polar dome centered in the space left of a dedicated legend column on the right (default window 1000×680) — nothing overlaps the dome, and the layout re-centers itself when the window is resized
- Polar dome: zenith `+` center, horizon circle at the edge, N top / E right / S bottom / W left. With `--fov` the edge becomes the field boundary (the `~`/circle) and the horizon is drawn as a curve wherever it crosses the field; only objects inside the field are drawn and listed in the legend
- White letters = brightest stars, **digits = brightest star clusters** (same shorthand as the terminal tool), silver dots = fainter ones, green lowercase initial = planet, yellow `O` = Moon, yellow `@` = Sun (only when above the horizon)
- **Stars in the legend show their Bayer designation** (Greek letter + genitive constellation, stored as `Alpha_Boo` style in `celestial_objects.csv`): e.g. `A Alpha_Lyr Vega`, `B Alpha_Aur Capella`. Clusters keep the digit shorthand: `1 C Hyades`, `2 C Alpha Persei Cluster`. Planets show name only: `O Moon`, `s Saturn`.
- Header lines: date/time (UTC) + moon illumination/phase, sun altitude + twilight state (day / civil / nautical / astronomical twilight / night), sunrise/sunset + civil twilight window, and a `view:` line giving the field centre + size (`(full sky)`, `(limited)` or the `(window az ..-.., alt ..-..)` bounds in window mode), the follow state (`follow: off` / `follow: on (Vega)`), and the `[f] follow  [x] window  [+/-] zoom  [w/a/s/d] pan  [0] full sky` hints
- Tracking line (only with a selection): `mount: AZ .. ALT ..  dAZ .. dALT ..  sep .. deg  ON TARGET  (Ns ago)` — the mount's last sent position (from `mount_status.txt`, written by the autoobservatory) against the selected object's live position; lime when within 0.5°, grey "no position recorded" when nothing has been sent yet
- **Legend (right column, 8pt font)** — grouped into **Sun & Moon / Planets & Dwarfs / Stars & Clusters** (grey headers, empty groups omitted), each group brightest first, with Bayer designation + name + az/alt/mag on one line; click a line to select it. Collapsible via `L` or the View menu. Layout fits within the 240px legend column: variable-width name (no padding) followed by `alt XX.X  mag X.X`, longest line ~48 chars at 8pt ≈ 216px.

## Code layout

- `pas/astrop.pas` — the ephemeris unit: Julian date, GMST, az/alt, Kepler + light-time + IAU 2006 precession + aberration, Schlyter Moon, sun RA/Dec, twilight + sun-event scans, CSV loaders (now parse 10th `bayer` column), and `ResolveName` (the 1:1 counterpart of the C++ `object_name.h` used by `planetarium --select` and `autoobservatory --object`). No GUI dependencies, so it is reusable and unit-tested by `make check-gui`. `TCelestialObject` now has a `Bayer: string` field. It also carries the shared **limited-view projection**: `TSkyView`, `FullSkyView`, `ValidFov`, `ValidCenter`, `IsLimitedView`, and `ProjectSky` (the 1:1 counterpart of `sky_view.h`, pinned by `make check-view`), plus the **rectangular az/alt window** helpers (`TRectWindow`, `ValidRectWindow`, `RectAzDiff` shortest-arc azimuth difference, `InRectWindow`) used by the GUI's window mode.
- `pas/PlanetariumGUI.lpr` — the form: TPaintBox (dome), TTimer (live refresh), TTrackBar (mag limit), all interaction handlers. `TSkyPoint` now has a `Bayer: string` field propagated from stars; legend shows Bayer for stars, digit shorthand for clusters. A `Follow: Boolean` field plus the `f` key recentre `View` on the selected object in `DrawDome` before anything is drawn.
- `pas/PlanetariumGUI.lpi` — Lazarus project file (gtk2 widget set)

The math is a 1:1 port of the C++ `astro_math.h` / `planetary_objects.h`; `make check-gui` proves equivalence against the same two-epoch reference table the C++ tool uses.

## Related

- [[Autoobservatory Project]]
- [[Using The Planetarium]]
- [[Ephemeris Accuracy Verification]]
