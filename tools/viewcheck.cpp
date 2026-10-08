// viewcheck.cpp - unit test for the limited-view sky projection (sky_view.h).
//
// Three things are pinned here:
//   1. Backward compatibility: the default full-sky view (zenith centre, fov
//      180) must reproduce the original polar projection EXACTLY - the old
//      screen offset was u = (1 - alt/90) * sin(az), v = (1 - alt/90) * cos(az).
//      If a refactor changes the full-sky map, this fails.
//   2. Orientation: N up, E right, S down, W left at the default centre.
//   3. Limited views: the field radius scales linearly with fov, points past
//      fov/2 are rejected, and the view centre projects to the drawing centre.
// Plus the --fov/--center validation rules.
//
// Build & run via:  make check-view
#include <cstdio>
#include <cmath>
#include <string>
#include "sky_view.h"

static int failures = 0;

static void expectNear(const char* label, double got, double want, double tol = 1e-9)
{
    double err = std::fabs(got - want);
    bool ok = err <= tol;
    if (!ok) ++failures;
    printf("%s %-46s got %+.6f want %+.6f (err %.2e)\n",
           ok ? "ok  " : "FAIL", label, got, want, err);
}

static void expectBool(const char* label, bool got, bool want)
{
    bool ok = got == want;
    if (!ok) ++failures;
    printf("%s %-46s got %s want %s\n", ok ? "ok  " : "FAIL", label,
           got ? "true" : "false", want ? "true" : "false");
}

// The original planetarium projection, in normalised drawing units.
static void LegacyOffset(double az, double alt, double& u, double& v)
{
    double rr = 1.0 - alt / 90.0;             // 1 at the horizon, 0 at the zenith
    double a = (90.0 - az) * M_PI / 180.0;
    u = rr * std::cos(a);
    v = rr * std::sin(a);
}

int main()
{
    // 1. Default view == the legacy polar projection, exactly, everywhere.
    const SkyView full = FullSkyView();
    for (double az = 0.0; az < 360.0; az += 7.0)
        for (double alt = 0.0; alt <= 90.0; alt += 5.0)
        {
            double u = 0, v = 0;
            bool ok = ProjectSky(full, az, alt, u, v);
            if (!ok) { printf("FAIL default view rejected az %.1f alt %.1f\n", az, alt); ++failures; continue; }
            double lu, lv;
            LegacyOffset(az, alt, lu, lv);
            if (std::fabs(u - lu) > 1e-9 || std::fabs(v - lv) > 1e-9)
            {
                printf("FAIL legacy mismatch az %6.1f alt %5.1f: got (%+.6f,%+.6f) want (%+.6f,%+.6f)\n",
                       az, alt, u, v, lu, lv);
                ++failures;
            }
        }
    printf("%s default view matches the legacy polar projection on the grid\n",
           failures ? "FAIL" : "ok  ");

    // 2. Orientation at the default centre: N up, E right, S down, W left.
    { double u, v; ProjectSky(full, 0.0, 0.0, u, v);   expectNear("N horizon  u", u, 0.0); expectNear("N horizon  v", v, 1.0); }
    { double u, v; ProjectSky(full, 90.0, 0.0, u, v);  expectNear("E horizon  u", u, 1.0); expectNear("E horizon  v", v, 0.0); }
    { double u, v; ProjectSky(full, 180.0, 0.0, u, v); expectNear("S horizon  u", u, 0.0); expectNear("S horizon  v", v, -1.0); }
    { double u, v; ProjectSky(full, 270.0, 0.0, u, v); expectNear("W horizon  u", u, -1.0); expectNear("W horizon  v", v, 0.0); }
    { double u, v; ProjectSky(full, 123.0, 90.0, u, v); expectNear("zenith     u", u, 0.0); expectNear("zenith     v", v, 0.0); }

    // 3. Zoom: the field radius scales linearly with fov and the edge is at |r|=1.
    {
        SkyView z; z.centerAlt = 90.0; z.fov = 40.0; // half-field 20 deg
        double u, v;
        ProjectSky(z, 0.0, 80.0, u, v);  // 10 deg from the zenith -> halfway out
        expectNear("zoom fov40 alt80 v (half radius)", v, 0.5);
        expectBool("zoom fov40 alt70 (on the edge) is kept", ProjectSky(z, 0.0, 70.0, u, v), true);
        expectNear("zoom fov40 alt70 v (edge)", v, 1.0);
        expectBool("zoom fov40 alt69 (past the edge) is rejected", ProjectSky(z, 0.0, 69.0, u, v), false);
        expectNear("zoom fov40 alt69 v (rejected, stays 0)", v, 0.0);
    }

    // The centre of an off-zenith view projects to (0, 0).
    {
        SkyView z; z.centerAz = 210.0; z.centerAlt = 30.0; z.fov = 25.0;
        double u, v;
        expectBool("centre point is inside the view", ProjectSky(z, 210.0, 30.0, u, v), true);
        expectNear("centre u", u, 0.0);
        expectNear("centre v", v, 0.0);
        expectBool("60 deg away is outside a 25 deg field", ProjectSky(z, 210.0, 90.0, u, v), false);
        expectBool("10 deg away is inside a 25 deg field", ProjectSky(z, 210.0, 40.0, u, v), true);
    }

    // 4. --fov / --center validation.
    expectBool("fov 0 invalid",        ValidFov(0.0),   false);
    expectBool("fov -5 invalid",       ValidFov(-5.0),  false);
    expectBool("fov 180 invalid (full sky)", ValidFov(180.0), false);
    expectBool("fov 200 invalid",      ValidFov(200.0), false);
    expectBool("fov 179.9 valid",      ValidFov(179.9), true);
    expectBool("fov 40 valid",         ValidFov(40.0),  true);
    expectBool("fov 0.5 valid",        ValidFov(0.5),   true);

    expectBool("az -1 invalid",        ValidCenter(-1.0, 45.0),   false);
    expectBool("az 360 invalid",       ValidCenter(360.0, 45.0),  false);
    expectBool("az 359.9 valid",       ValidCenter(359.9, 45.0),  true);
    expectBool("alt 91 invalid",       ValidCenter(45.0, 91.0),   false);
    expectBool("alt -91 invalid",      ValidCenter(45.0, -91.0),  false);
    expectBool("alt -90 valid",        ValidCenter(45.0, -90.0),  true);
    expectBool("alt 90 valid",         ValidCenter(45.0, 90.0),   true);

    expectBool("full sky is not limited",  IsLimitedView(FullSkyView()), false);
    { SkyView z = FullSkyView(); z.fov = 60.0;  expectBool("smaller fov is limited", IsLimitedView(z), true); }
    { SkyView z = FullSkyView(); z.centerAlt = 45.0; expectBool("off-zenith is limited", IsLimitedView(z), true); }

    if (failures) { printf("\n%d check(s) FAILED\n", failures); return 1; }
    printf("\nAll sky-view checks passed.\n");
    return 0;
}
