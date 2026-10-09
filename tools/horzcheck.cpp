// horzcheck.cpp - regression check for the HORIZONTAL-coordinate path.
//
// Every other check in `make check` works in equatorial coordinates (RA/Dec).
// That left GreenwichMeanSiderealTime -> CalculateHorizontalCoordinates -- the
// only code that actually aims the mount -- completely unverified, and it took
// a real bug to notice: celestial_objects.csv rows are J2000 but were fed
// straight into the horizontal transform with no precession, leaving the dome
// and the mount 9-21 arcmin off (larger than the firmware DEADBAND_DEG).
//
// This tool feeds catalog J2000 coordinates through the project's own
// precess + horizontal transform and compares against astropy 8.0.1 apparent
// azimuth/altitude at the same instant and site.
//
// Expected residual: the project applies IAU 2006 precession but not nutation
// or annual aberration, so ~0.01 deg is expected (nutation <=17" + aberration
// <=20.5"). Tolerance is 0.05 deg -- loose enough for those, an order of
// magnitude tighter than the 0.15-0.35 deg the missing precession caused.
//
// Each epoch also runs a NEGATIVE CONTROL: the same object with precession
// skipped must FAIL the tolerance. Without that, a test that silently stopped
// precessing (or a tolerance widened by accident) would still pass.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "astro_math.h"
#include "planetary_objects.h"

struct HCheck
{
    const char* name;
    double raH, raM, raS;
    double decSign, decD, decM, decS;
    double az, alt; // astropy apparent, degrees
};

static const double kTolDeg = 0.05;

// 45N 10E, 21:00 UTC. Reference: astropy 8.0.1, SkyCoord(frame='icrs')
// .transform_to(AltAz(obstime, location)).
static const HCheck epoch1[] = {
    { "Vega",     18, 36, 56,  1, 38, 47,  0,   277.48198,  54.09447 },
    { "Capella",   5, 16, 59,  1, 45, 59,  0,    42.89897,  18.94123 },
    { "Arcturus", 14, 15, 10,  1, 19, 10,  0,   299.92482,  -2.12435 },
    { "Polaris",   2, 31,  0,  1, 89, 15, 36,    0.87709,  45.11718 },
    { "Antares",  16, 29, 24, -1, 26, 25, 54,   244.21621, -11.83510 },
};

static const HCheck epoch2[] = {
    { "Vega",     18, 36, 56,  1, 38, 47,  0,    34.61148,   3.72026 },
    { "Capella",   5, 16, 59,  1, 45, 59,  0,   296.12006,  44.87709 },
    { "Arcturus", 14, 15, 10,  1, 19, 10,  0,    91.29588,  28.75631 },
    { "Polaris",   2, 31,  0,  1, 89, 15, 36,   359.13575,  44.89011 },
    { "Antares",  16, 29, 24, -1, 26, 25, 54,   102.08159, -26.30892 },
};

// True angular separation on the sphere, not a Euclidean az/alt distance.
static double SeparationDeg(double az1, double alt1, double az2, double alt2)
{
    double a1 = DegreesToRadians(alt1), a2 = DegreesToRadians(alt2);
    double dA = DegreesToRadians(az2 - az1);
    double cosSep = std::sin(a1) * std::sin(a2) + std::cos(a1) * std::cos(a2) * std::cos(dA);
    if (cosSep > 1.0) cosSep = 1.0;
    if (cosSep < -1.0) cosSep = -1.0;
    return RadiansToDegrees(std::acos(cosSep));
}

static int RunEpoch(const HCheck* checks, int n,
                    double y, double m, double d, double h,
                    const char* label, bool negativeControl)
{
    int failures = 0;
    const DateTime when{y, m, d, h, 0.0, 0.0, 0.0};
    const Observer site{45.0, 10.0};
    double jd = when.JulianDay();
    for (int i = 0; i < n; ++i)
    {
        const HCheck& c = checks[i];
        double ra  = c.raH + c.raM / 60.0 + c.raS / 3600.0;
        double dec = c.decSign * (c.decD + c.decM / 60.0 + c.decS / 3600.0);
        if (!negativeControl)
            PrecessRaDecJ2000ToDate(jd, ra, dec); // J2000 -> mean of date
        auto [az, alt] = CalculateHorizontalCoordinates(when, site, ra, dec);

        double sep = SeparationDeg(az, alt, c.az, c.alt);
        bool ok = sep <= kTolDeg;
        if (!ok) ++failures;

        printf("%s %-9s sep %7.4f deg (tol %.2f)  az %8.3f vs %8.3f  alt %8.3f vs %8.3f  [%s]\n",
               ok ? "ok  " : "FAIL", c.name, sep, kTolDeg, az, c.az, alt, c.alt, label);
    }
    return failures;
}

int main()
{
    int failures = 0;

    printf("== horizontal-coordinate check (J2000 catalog -> precess -> az/alt) ==\n");
    printf("   reference: astropy 8.0.1 apparent place, 45N 10E\n");
    failures += RunEpoch(epoch1, 5, 2026, 9, 23, 21.0, "2026-09-23", false);
    failures += RunEpoch(epoch2, 5, 2027, 3, 23, 21.0, "2027-03-23", false);

    // Negative control: skipping precession MUST break these. If it does not,
    // the tolerance is too loose and the check above proves nothing.
    printf("== negative control: same data WITHOUT precession (must FAIL) ==\n");
    int controlFailures = 0;
    controlFailures += RunEpoch(epoch1, 5, 2026, 9, 23, 21.0, "no-precession", true);
    if (controlFailures == 0)
    {
        printf("\nERROR: negative control passed -- tolerance is too loose to detect\n"
               "       the missing-precession bug. Tighten kTolDeg.\n");
        return 1;
    }
    printf("negative control correctly failed (%d/%d objects outside tolerance).\n",
           controlFailures, 5);

    if (failures)
    {
        printf("\n%d horizontal-coordinate check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll horizontal-coordinate checks passed.\n");
    return 0;
}