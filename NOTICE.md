# NOTICE — third-party sources and licenses

This file records where the bundled data and generated tables come from, and the
license each source carries. It is a factual inventory assembled on 2026-10-08,
not legal advice. If you intend to redistribute this project, confirm the
obligations below with someone qualified to advise on licensing.

## This project's own code

**GPL-3.0** — see [`LICENSE`](LICENSE). This covers the C++ (`*.cpp`, `*.h`), the
Free Pascal sources in `pas/`, the Arduino sketch in `arduino/`, and everything in
`tools/` written for this project.

Chosen deliberately over a permissive license, so that any distributed derivative
must also publish its source. The repo is currently **private**, so the
copyleft obligation is not yet triggered: nothing has been conveyed to anyone, and
GPL obligations attach on distribution. Making the repo public later is the moment
that starts them.

Compatibility with the bundled material, for reference:

- **LGPL v3** VSOP87 tables — GPLv3 is on the LGPLv3's list of compatible
  licenses, so the generated tables do not conflict with GPL-3.0.
- **CC BY-SA 4.0** catalog data — GPLv3 and CC BY-SA 4.0 are listed as mutually
  compatible under GPLv3's compatibility provisions, so shipping the catalog
  alongside GPL-3.0 code is not a license conflict. The catalog is offered under
  CC BY-SA 4.0 on its own terms regardless; see below.

## Summary

| Source | What it provides here | License |
|---|---|---|
| [HYG database v4.1](https://codeberg.org/astronexus/hyg) — David Nash / Astronexus | 434 Bayer-designated stars: J2000 positions, V magnitudes, proper names, Bayer designations | **CC BY-SA 4.0** |
| [OpenNGC](https://github.com/mattiaverga/OpenNGC) — Mattia Verga | 460 clusters and nebulae: NGC/IC positions, magnitudes, common names | **CC BY-SA 4.0** |
| [PyMeeus](https://github.com/architest/pymeeus) — Dagoberto Salazar | VSOP87 tables used to generate `vsop87_trunc.h` and `pas/vsop87_trunc.inc` | **LGPL v3** |
| [JPL Horizons](https://ssd.jpl.nasa.gov/horizons/) / [SBDB](https://ssd.jpl.nasa.gov/sbdb/) — NASA/JPL | Osculating elements, absolute magnitude `H`, phase slope `G`, and the reference ephemerides used by `tools/ephcheck.cpp` | U.S. Government work — **public domain** |
| [WMM2025](https://www.ngdc.noaa.gov/) — NGA | `WMM2025.COF` geomagnetic model coefficients and `wmm_mag.c` / `wmm_mag.h` | U.S. Government work — **public domain** |
| Paul Schlyter — [stjarnhimlen.se](https://stjarnhimlen.se/comp/ppcomp.html) | Lunar and planetary position formulae (algorithms published as prose, not code) | **No license stated** — cited as a source |

## Detail

### CC BY-SA 4.0 — copyleft attribution obligations

The rows of `celestial_objects.csv` are a **modified adaptation** of CC BY-SA 4.0
licensed data. Two obligations follow:

1. **Attribution.** Both sources must be credited. Done in the header block of
   `celestial_objects.csv` itself and here.
2. **Share-alike on the adapted material.** CC BY-SA 4.0 is copyleft: adaptations
   must be distributed under the same terms. The catalog is offered under
   CC BY-SA 4.0 accordingly.

Note the scope: the share-alike obligation attaches to the adapted *data*. Whether
it extends to the surrounding program is the well-known collection/database
distinction under CC, and is **not** settled law in every jurisdiction. This
project's position is stated conservatively in `LICENSE`.

`OpenNGC` is itself a compilation: it merges data from NED (NASA/IPAC),
HyperLEDA, SIMBAD (CDS), HEASARC and Harold Corwin's NGC/IC positions. Attribution
to Mattia Verga as the compiler is what OpenNGC's own license requires;
attribution to the underlying archives is good practice and is noted above.

HYG v4.0+ is CC BY-SA 4.0; earlier versions were CC BY-SA 2.5. This project uses
**v4.1**. The GitHub mirror (`astronexus/hyg-database`) is archived — the current
home is Codeberg.

### LGPL v3 — the VSOP87 tables

`vsop87_trunc.h` and `pas/vsop87_trunc.inc` are **generated data files** produced
by `tools/vsop87_gen.py` from the VSOP87 tables shipped with PyMeeus, which is
LGPL v3. Both generated files carry this attribution in their headers.

The underlying VSOP87 solutions are work by Bretagnon & Francou (IMCCE); the
series data is also published by IMCCE. Nothing in this repository alters the
LGPL status of those tables. If you replace or wrap this project in a way that
constitutes a derivative library rather than mere generated data, read the LGPL
obligations carefully.

### Public domain — courtesy citation only

JPL and NGA products carry no license obligation. Citation is still good
scholarship practice, and the reference values baked into `tools/ephcheck.cpp`
and `pas/ephcheck_pas.lpr` were fetched from the Horizons API on 2026-10.

### Schlyter — no license grant available

Paul Schlyter's tutorial publishes *algorithms* (formulae and worked numeric
examples), explicitly not code. The site states no license, so there is nothing
to grant or comply with. His own account of the method credits a preprint by
T. van Flandern and K. Pulkkinen, *Low precision formulae for planetary
positions*, ApJ Supp. Series (1980).

The implementation in `planetary_objects.h` (`ComputeMoonGeocentric` and the
Schlyter element sets) was written for this project and is covered by
`LICENSE`. It is cited here as a methodological source, and no rights in
Schlyter's text are claimed or exercised.