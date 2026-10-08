---
tags:
  - astronomy
  - math
  - ephemeris
  - reference
---
# Astronomy Math Reference

Techniques used in the [[Autoobservatory Project]] position math.

## Keplerian propagation from mean elements

Element = value_J2000 + rate × (jd − 2451545.0). Solve Kepler's equation by Newton iteration, rotate perihelion→ecliptic→equatorial, convert to RA/Dec. Consistency rule: mean motion must satisfy n = 0.9856076686 / a^1.5 (deg/day) — Saturn's CSV violated this and drifted 0.71° per 26.7 years.

## Light-time correction

Iterate ~3×: evaluate the planet at jd − dist/173.15 days (1 AU = 173.15 light-days) and use the retarded geocentric vector. Converges to <1″.

## IAU 2006 precession

ζ, z, θ polynomial angles (Capitaine et al.) built into R3(−z)·R2(θ)·R3(−ζ). ~0.1″ accurate for centuries around J2000. Applied only to J2000-frame vectors (CSV planets) — never to Schlyter Moon output, which is already equinox-of-date (applying it double-counts, ~20′ error).

## Annual aberration

Add k·v_earth (k = 0.0057755183 days/AU = 1/c) to the geocentric vector; Earth's velocity via finite-differencing its heliocentric position over ±0.5 days. First-order covers ~99.5% of the ~20.5″ effect. Signature it's correct: transverse component nearly vanishes for outer planets near opposition.

## H-G phase function (minor bodies)

Asteroids and distant small bodies (CSV rows with phase slope `G > 0`) use the IAU H-G model:

`mag = H + 5·log10(r·d) − 2.5·log10(Φ(α))`, with `Φ(α) = (1−G)·Φ1 + G·Φ2`,
`Φ1 = exp(−3.33·tan(α/2)^0.63)`, `Φ2 = exp(−1.87·tan(α/2)^1.22)`,
and α the Sun-object-observer phase angle. The distance-only form `H + 5·log10(r·d)` is only correct at opposition, so without this an asteroid fades/over-brightens by up to several magnitudes as its phase angle grows. Verified against JPL Horizons apparent V magnitudes (11 objects, ≤0.01 mag). `H` and `G` come from Horizons — true absolute magnitudes, since every minor-body orbit except Pluto's now uses Horizons osculating elements at (or back-propagated to) J2000; the eight planets keep the Schlyter zero-points (now carried by the VSOP87 code path and mirrored in the CSV H column), and Mercury/Venus use a Lambert phase term `−2.5·log10((1+cos α)/2)` instead. The Schlyter phase model diverges from Horizons APmag at large phase angles (Mercury ~1.7 mag worst near inferior conjunction); positions are unaffected.

**Regression guard:** `HGPhase()` lives in one place per language (`planetary_objects.h` / `astrop.pas`) and is pinned by a frozen-value unit test (`tools/hgcheck.cpp` + `pas/hgcheck_pas.lpr`, run by `make check-hg` inside `make check`). 19 (G, α, Φ) cases — including G ≤ 0 short-circuit and α up to 120° — must produce identical output in both languages; any edit to either implementation that changes a value fails the suite. Note the α = 0 cases all yield Φ = 1 exactly (Φ1 = Φ2 = 1), and the tangent is taken on the radian half-angle — tan(60°) ≈ 1.73, not tan(60 rad).

## Limited views (azimuthal equidistant projection)

The full-sky map is an azimuthal equidistant projection about the zenith: the angular distance from the centre (90° − altitude) maps linearly to the drawing radius. A limited (`--fov`) view generalises the same projection to an arbitrary centre (az₀, alt₀):

- tangent-plane components at the centre: `x = cos h·sin(ΔA)`, `y = −sin h₀·cos h·cos(ΔA) + sin h·cos h₀` (with ΔA = az − az₀)
- angular distance θ from `cos θ = sin h₀·sin h + cos h₀·cos h·cos ΔA`
- `u = (θ / (fov/2)) · x/sin θ` (screen right), `v = (θ / (fov/2)) · (−y/sin θ)` (screen **up**, then y is negated when drawing because screen y grows downward)

Points with θ > fov/2 are outside the field and are not drawn. At the default centre (az₀ = 0, alt₀ = 90, fov = 180) this reduces **exactly** to the original `u = (1 − alt/90)·sin(az)`, `v = (1 − alt/90)·cos(az)`, which `tools/viewcheck.cpp` and `pas/viewcheck_pas.lpr` pin on a grid of az/alt (so the full-sky map cannot drift). The `−y`/`−sin θ` keeps the planisphere mirror — increasing altitude is toward the drawing centre — so a zoom is a magnified patch of the existing map, N up / E right. The fov is a **diameter**, so `fov = 180` is the whole visible hemisphere and valid inputs must satisfy `0 < fov < 180`. A hair of tolerance (`+1e-12` rad) keeps points exactly on the field edge (the horizon at fov 180), where `cos()` rounding lands θ a few ULP either side of fov/2 depending on azimuth.

**Follow mode** adds no new math: it simply drives the centre (az₀, alt₀) each frame from the locked target's current horizontal coordinates, so the same projection tracks the object as the sky turns.

## Schlyter lunar model

Elements measured from 1999-12-31 0h UT (JD 2451543.5) — NOT J2000 noon. Perturbation terms (evection −1.274°, variation +0.658°, yearly equation −0.186°, +9 more longitude, 5 latitude, 2 distance) must derive D = Lm − Ls and F = Lm − N from the SAME element set; mixing in Meeus mean-element constants inverts improvements.

## Residual budget

| Effect | Size |
|---|---|
| Mean-elements model floor | ~9–17′ (planets) |
| Missing nutation | ~17″ |
| Missing aberration 2nd order | <1″ |
| Moon (Schlyter + main terms) | ~1–8′ |
| Osculating-element minor bodies | ~0.2–0.3° (linear propagation of osculating elements, no perturbations) |

## Related

- [[Autoobservatory Project]]
- [[Ephemeris Accuracy Verification]]
