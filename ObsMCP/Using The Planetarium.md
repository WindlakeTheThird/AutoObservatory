# Using The Planetarium

Everyday commands for the `planetarium` binary from the [[Autoobservatory Project]]. Build it once with `make`, run from the project directory (it needs `celestial_objects.csv` and `planetary_objects.csv` alongside).

## Quick start

```bash
./planetarium              # tonight's sky at 45N 10E (defaults), right now
./planetarium --help       # full usage
```

You get an ASCII sky dome: zenith `+` at center, the outer circle is the horizon, **N top / E right / S bottom / W left**. Objects farther from center are lower in the sky. Below-horizon objects are not drawn.

## Everyday commands

```bash
# Your location (latitude longitude: decimal degrees or DEG MIN SEC HEMI;
# south/west negative — either via a minus sign or the hemisphere letter)
./planetarium 51.5 -0.1            # London, decimal
./planetarium 51 30 N 0 7 W        # London, degrees-minutes + hemisphere
./planetarium -33.9 151.2          # Sydney
./planetarium 33 51 S 151 12 E     # Sydney, DMS-style

# A specific night (all times UTC; malformed or impossible values are rejected)
./planetarium 45 10 --date 2026-10-15 --time 21:00

# Fainter objects too (magnitude limit; default 4.0)
./planetarium 45 10 --mag 6.0      # binocular-sky view

# Bright objects only — good for a quick naked-eye look
./planetarium 45 10 --mag 3.0

# Live sky — redraws from the real clock every 10 s (default), Ctrl-C not needed
./planetarium --live
./planetarium --live 30           # refresh every 30 s instead

# Leave live mode with q (or Q) — terminal is restored and it prints
# "Planetarium closed." and exits with code 0.
```

## Zooming in on one part of the sky

`--fov` narrows the map to a circular field and `--center` aims it; both also work in live mode, where you can zoom and pan without restarting.

```bash
# A 30-degree field centred on the zenith (a straight-up, high-power view)
./planetarium 45 10 --fov 30

# A 24-degree field around Vega (az/alt from the full-sky legend)
./planetarium 45 10 --fov 24 --center 289.2,39.0

# Live, then press +/- to zoom and w/a/s/d to pan; 0 snaps back to the full sky
./planetarium --live --fov 40

# Lock a target and follow it: the field re-centres on Vega every frame
./planetarium --live --fov 40 --select Vega --follow
```

- `--fov DEG` is the angular **diameter** of the field. It must be **smaller than a full sky** (`0 < fov < 180`); omitting it shows the whole sky. Anything `<= 0` or `>= 180` is a usage error (exit 2).
- `--center AZ,ALT` (or `--center AZ ALT`) points the field: azimuth `[0,360)` (0 = N, 90 = E) and altitude `[-90,90]` (90 = zenith). Default `0,90` (the zenith).
- The map keeps its planisphere orientation (N up / E right), so this is literally a magnified patch of the full-sky map. The `~` ring is the edge of the chosen field; the `.` horizon is drawn wherever it crosses. Only objects inside the field appear, on the dome and in the legend.
- Outside live mode a limited view is a still image; in live mode the `+`/`-`, `w`/`a`/`s`/`d`, and `0` keys adjust it on the fly.
- **Follow mode** (`--follow`, or press `f` in live mode) keeps the view centred on the **locked target** as the sky turns: the centre is reset to the target's azimuth/altitude every frame. It needs a target — `--select NAME`, or live `n`/`p` to walk the cursor and `t` to lock one. Zooming keeps following the target; panning with `w`/`a`/`s`/`d` (or `0` for the full sky) is a manual override and stops it, so the keys always do what you expect. The `view:` line reports the state (`follow: on (Vega)` / `follow: off`).

## Reading the map

- **Stars carry their Bayer designation in the legend** (Greek letter + genitive constellation, e.g. `Alpha_Lyr Vega`, `Alpha_Boo Arcturus`, `Beta_Ori Rigel`) for a more professional map. Clusters keep the **digit shorthand** (`1 = C Hyades`, `2 = C Alpha Persei Cluster` — letters = Stars, digits = Clusters). The legend under the map gives each one's Bayer/name, azimuth, altitude, and magnitude.
- **O** is the Moon; **planets use their lowercase initial** (`s` Saturn, `j` Jupiter, `c` Ceres…).
- **Dots** are fainter objects above the magnitude limit. The catalog holds ~900 objects: every Bayer-designated star brighter than magnitude 4 across the constellations, and 460 clusters and nebulae (the full Messier set plus all clusters/nebulae brighter than magnitude 10). Raise `--mag` / the trackbar to reveal them — the dome draws down to magnitude 10, the faint end of the catalog. To look away from the zenith, use `--center AZ,ALT` or the live `w`/`a`/`s`/`d` keys; altitudes below 0 aim the field under the horizon.
- **@** is the Sun — only drawn when it's above the horizon (it outshines everything else).
- The header shows date/time (with seconds in live mode), observer, Moon illumination, and phase. The next lines give the **Sun's current altitude and twilight state** (`day` / `civil twilight` / `nautical twilight` / `astronomical twilight` / `night`), the **Sun's rise and set times plus the civil twilight window** (`sun rise 05:10 UTC  set 17:15 UTC  |  civil 04:40-17:45 UTC`), and the **Moon's rise/set times**.
- The `view:` line states the current centre and field: `fov 180.0 deg (full sky)` by default, or `view: center az ... alt ... fov ... deg (limited)` when `--fov`/`--center` (or a live zoom) is active, followed by `follow: off` or `follow: on (TARGET)` when follow mode is steering the view.
- In live mode a `[q] quit` hint plus the zoom/pan key legend sit under the map.

## Example session

```
$ ./planetarium 45 10 --date 2026-09-23 --time 20:00
PLANETARIUM  2026-09-23 20:00 UTC
Observer: 45.0N, 10.0E  |  Mag limit 4.0
Moon illumination ~81% | Waxing Gibbous | rises 15:35, sets 00:10

                               N
                       ................
                  .....                .....        <- ring = horizon
               ...        + (zenith)          ...
             ...   V                            ..     <- Vega, high in W
            .                          D          .   <- Deneb near zenith
           .        A                               .   <- Altair, S
            ...    O (Moon, SSW)  s (Saturn, E) ...
                  .....                .....
```

(Typical layout — your output will show the real object letters.)

## Tips

- **Watch the pointing error, not just the target.** Once a target is locked, the header shows the mount's last sent AZ/ALT next to the object's live position, with `dAZ`, `dALT` and the true angular `sep`. If `sep` stops shrinking the mount is stuck (or the serial link is silent — the `(Ns ago)` age tells you which). The autoobservatory writes this telemetry to `mount_status.txt` only when it actually sends a line, so a dead zone, a stalled motor, or a closed port all show up as a stale age.
- **Selecting a target for the mount**: in `--live` mode press `n`/`p` (or Tab) to walk the object cursor, `t` to lock the highlighted object, `c` to clear, `q` to quit. For scripts and one-liners use `./planetarium 45 10 --select Vega` instead — same result, no keypresses. Matching is case-insensitive, with fuzzy fallback: a unique prefix or substring resolves (`--select arctur` finds Arcturus), and the printed line says so. An ambiguous query lists the candidates and exits 1, and a name that matches nothing exits 1 — so a typo never silently aims the mount at whatever was selected last. Exit codes: 0 success, 1 runtime error (unknown `--select` name), 2 usage error (bad number, unknown option, invalid lat/lon). `./autoobservatory --object NAME` uses the same shared resolver, so the same spellings work there too. The locked name goes to `selected_target.txt`, so the next step is just `./autoobservatory <lat> <lon> --from-planetarium -a --serial /dev/ttyUSB0` — no retyping names, no risk of a typo pointing the mount at the wrong coordinate. `[ ]` frames the target on the dome, `( )` the cursor; the legend marks them with `*` and `>`.
- **Live mode is the natural way to use it** — leave `./planetarium --live` running in a spare terminal; the map, Sun/twilight line, and Moon times all update themselves as the sky turns. Zoom in on one constellation with `+`/`-` and pan with `w`/`a`/`s`/`d` (`0` returns to the full sky) without disturbing the target cursor, and press `f` once a target is locked to let the field follow it across the sky. Time stands still in one-shot mode only.
- **Full moon washes out faint stars** — try `--mag 3` on nights near full, `--mag 5.5` around new moon.
- Times are **UTC**: subtract your local offset (e.g. `--time 20:00` UTC = 22:00 CEST).
- The same position math drives the autoobservatory mount tracking, so what you see on the map is what the telescope points at.

## Related

- [[Autoobservatory Project]]
- [[Pascal Planetarium GUI]] — the Form-app version of this tool
- [[Project Index]]
