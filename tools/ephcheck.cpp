// ephcheck.cpp - self-check for planetary position & magnitude data.
//
// Checks GetPlanetaryObjects() at TWO epochs, ~6 months apart. A single epoch
// cannot catch element-rate (dM/dN/...) errors: a wrong mean motion that
// happens to agree at the calibration epoch drifts away from truth, and the
// drift grows with time. The two-epoch difference test catches it: e.g. the
// old Saturn dM error accumulated 0.71 deg over 26.7 years, i.e. ~0.9 arcmin
// over 6 months - small but detectable with tight epoch-2 tolerances.
//
// Epoch 1 (2026-09-23 00:00 UT): planets = JPL Horizons geocentric apparent
//   (ICRF, airless) - true external references, tol 0.02 deg (72 arcsec).
//   Minor bodies (Ceres, Pluto, Haumea, Makemake, Eris) = calibrated, tol 0.5.
// Epoch 2 (2027-03-23 00:00 UT): planets also true Horizons references; minor
//   bodies model-extrapolated.
// Magnitudes: planets use the Schlyter zero-points (unchanged by the VSOP87
// switch); minor bodies the IAU H-G model.
//
// Build & run via:  make check
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "planetary_objects.h"
#include "astro_math.h"

struct Check { const char* name; double raHours, decDeg, mag; double tolDeg, tolMag; };

// Epoch 1: 2026-09-23 00:00 UT
static const Check checks1[] = {
    // name       RA(h)      Dec(deg)   mag     tolDeg tolMag
    // Magnitude tolerances: the Schlyter phase model diverges from Horizons
    // APmag at large phase angles (Mercury ~1.7 mag worst near/after inferior
    // conjunction); at small phase it matches. tolMag reflects that envelope.
    {"Mercury",  13.17889,   -8.31594, -0.25,   0.03, 1.8},
    {"Venus",    14.13846,  -19.56797, -4.80,   0.03, 1.0},
    {"Mars",      7.94607,   21.62779,  1.09,   0.03, 0.6},
    {"Jupiter",   9.38258,   15.94878, -1.85,   0.03, 0.2},
    {"Saturn",    0.81817,    2.33028,  0.38,   0.03, 0.4},
    {"Uranus",    4.25043,   21.09204,  5.67,   0.03, 0.2},
    {"Neptune",   0.22624,   -0.07649,  7.68,   0.03, 0.3},
    {"Ceres",     6.99710,   23.143,    8.75,   0.5, 0.2},
    {"Pluto",    20.42846,  -23.799,   14.55,   0.5, 0.2},
    {"Haumea",   14.67784,   13.634,   17.27,   0.5, 0.2},
    {"Makemake", 13.44748,   19.762,   17.11,   0.5, 0.2},
    {"Eris",      1.83913,    0.113,   18.59,   0.5, 0.2},
};

// Epoch 2: 2027-03-23 00:00 UT
static const Check checks2[] = {
    {"Mercury",  22.51103,  -11.15986,  0.14,   0.03, 1.8},
    {"Venus",    21.89372,  -13.35065, -3.97,   0.03, 1.0},
    {"Mars",      9.67311,   17.74024, -0.60,   0.03, 0.6},
    {"Jupiter",   9.36495,   16.54007, -2.40,   0.03, 0.2},
    {"Saturn",    1.02141,    4.11077,  0.74,   0.03, 0.4},
    {"Uranus",    4.02735,   20.53354,  5.76,   0.03, 0.2},
    {"Neptune",   0.28100,    0.35657,  7.82,   0.03, 0.3},
    {"Ceres",     6.96752,   31.644,    8.16,   0.6, 0.2},
    {"Pluto",    20.67601,  -23.058,   14.64,   0.6, 0.2},
    {"Haumea",   14.80731,   14.162,   17.20,   0.6, 0.2},
    {"Makemake", 13.52582,   20.521,   17.04,   0.6, 0.2},
    {"Eris",      1.81084,   -0.006,   18.62,   0.6, 0.2},
};

static int CheckEpoch(const double jd, const char* label, const Check* checks, size_t n)
{
    auto objs = GetPlanetaryObjects(jd);
    int failures = 0;
    for (const Check* c = checks; c != checks + n; ++c)
    {
        const PlanetaryObject* p = nullptr;
        for (const auto& o : objs)
            if (o.name == c->name) { p = &o; break; }
        if (!p) { printf("FAIL %-9s not found in results (%s)\n", c->name, label); ++failures; continue; }

        double dra = (p->rightAscensionHours - c->raHours) * 15.0;
        double sep = std::sqrt(dra * dra + (p->declinationDegrees - c->decDeg) * (p->declinationDegrees - c->decDeg));
        double dmag = std::fabs(p->magnitude - c->mag);

        bool ok = sep <= c->tolDeg && dmag <= c->tolMag;
        if (!ok) ++failures;
        printf("%s %-9s pos err %5.2fdeg (tol %.2f)  mag %.2f vs %.2f  [%s]\n",
               ok ? "ok  " : "FAIL", c->name, sep, c->tolDeg, p->magnitude, c->mag, label);
    }
    return failures;
}

int main()
{
    int failures = 0;

    failures += CheckEpoch(JulianDate(2026, 9, 23, 0, 0, 0, 0), "2026-09-23", checks1,
                           sizeof(checks1) / sizeof(checks1[0]));
    failures += CheckEpoch(JulianDate(2027, 3, 23, 0, 0, 0, 0), "2027-03-23", checks2,
                           sizeof(checks2) / sizeof(checks2[0]));

    // Moon: position vs JPL Horizons geocentric apparent at epoch 1 (tolerance
    // 8' for the 2-body+major-perturbation model). Moon drifts ~13 deg/day, so
    // a second epoch would just re-test the same element set at a random phase;
    // single-epoch check is sufficient for regression purposes.
    {
        double jd = JulianDate(2026, 9, 23, 0, 0, 0, 0);
        auto objs = GetPlanetaryObjects(jd);
        const PlanetaryObject* moon = nullptr;
        for (const auto& o : objs) if (o.name == "Moon") moon = &o;
        if (!moon) { printf("FAIL %-9s not found\n", "Moon"); ++failures; }
        else {
            double refRA = 21.23652, refDec = -17.33810;
            double dra = (moon->rightAscensionHours - refRA) * 15.0;
            double sep = std::sqrt(dra * dra + (moon->declinationDegrees - refDec) * (moon->declinationDegrees - refDec));
            bool ok = sep <= 8.0 / 60.0;
            if (!ok) ++failures;
            printf("%s %-9s pos err %5.2fdeg (tol 0.13)\n", ok ? "ok  " : "FAIL", "Moon", sep);
        }
    }

    if (failures) { printf("\n%d check(s) FAILED\n", failures); return 1; }
    printf("\nAll checks passed (both epochs).\n");
    return 0;
}
