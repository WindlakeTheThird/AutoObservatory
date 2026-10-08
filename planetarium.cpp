// planetarium.cpp - ASCII sky-dome map: looking UP at the whole sky.
// Polar projection: zenith at center, horizon circle at edge, N at top,
// E right, S bottom, W left (matches a planisphere held overhead).
// Build: g++ -Wall -O2 -o planetarium planetarium.cpp wmm_mag.c -lm
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <algorithm>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include "astro_math.h"
#include "celestial_objects.h"
#include "celestial_object_reader.h"
#include "planetary_objects.h"
#include "object_name.h"
#include "cli_parse.h"
#include "sky_view.h"

// 61x31 grid: wide aspect (chars are ~2x tall as wide)
static const int W = 61, H = 31;
static const double CX = W / 2.0, CY = H / 2.0;
static const double RADIUS = H / 2.0 - 1.0;

struct SkyObject { std::string name; double az, alt, mag; bool isPlanet; char sym; std::string bayer; };

static char grid[H][W];

// Current view: full sky (zenith centre, hemisphere field) unless --fov /
// --center narrow it, or the live-mode zoom/pan keys change it.
static SkyView g_view;

// Follow mode (--follow, live key f): keep the view centred on the locked
// target every frame, so a limited field tracks it as the sky turns. Off by
// default; recentring only happens when a target is set.
static bool g_follow = false;

// --- Target hand-off to the autoobservatory ---------------------------------
// In live mode the user cycles with n/p and locks a target with t; the name is
// written to this file, and ./autoobservatory --from-planetarium picks it up
// (or pass the name straight to --object). Only the name is exchanged, so the
// planetarium stays the authority on what is in the sky.
static const char* TARGET_FILE = "selected_target.txt";
static std::vector<SkyObject> g_plotted; // last frame's selectable objects
static std::string g_cursorName;         // highlighted while cycling
static std::string g_targetName;         // locked in for the autoobservatory

// Human-readable follow state for the header line and the live key hints.
static std::string FollowState()
{
    if (!g_follow) return "off";
    if (g_targetName.empty()) return "on (no target locked)";
    return "on (" + g_targetName + ")";
}

static void WriteTargetFile(const std::string& name)
{
    std::ofstream f(TARGET_FILE);
    if (f.is_open()) f << name << "\n";
}

// Telemetry published by ./autoobservatory when it sends a position to the
// mount: one line "AZ:<deg> ALT:<deg> [TARGET:<name>] T:<epoch>".
static const char* MOUNT_STATUS_FILE = "mount_status.txt";

static bool ReadMountStatus(double& az, double& alt, double& ageSec)
{
    std::ifstream f(MOUNT_STATUS_FILE);
    if (!f.is_open()) return false;
    std::string line;
    if (!std::getline(f, line)) return false;
    double t = 0.0;
    bool gotAz = false, gotAlt = false;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok)
    {
        try
        {
            if (tok.rfind("AZ:", 0) == 0)  { az = std::stod(tok.substr(3)); gotAz = true; }
            else if (tok.rfind("ALT:", 0) == 0) { alt = std::stod(tok.substr(4)); gotAlt = true; }
            else if (tok.rfind("T:", 0) == 0)   { t = std::stod(tok.substr(2)); }
        }
        catch (...) { return false; }
    }
    if (!gotAz || !gotAlt) return false;
    ageSec = t > 0.0 ? (double)std::time(nullptr) - t : 0.0;
    return true;
}

// Common fields for the tracking panel; cached per frame.
static void PrintTrackingPanel(const std::string& targetName, double lat, double lon,
                               double year, double month, double day,
                               double hour, double minute, double second)
{
    if (targetName.empty()) return;
    double tAz = 0, tAlt = 0;
    bool haveTarget = false;
    std::string liveObj;
    for (auto& o : g_plotted)
        if (o.name == targetName)
        {
            tAz = o.az; tAlt = o.alt;
            haveTarget = true;
            break;
        }
    if (!haveTarget)
    {
        // fainter than the current mag limit: still report where it is
        std::vector<CelestialObject> stars;
        try { stars = CelestialObjectReader::ReadFromCSV("celestial_objects.csv"); } catch (...) {}
        for (auto& s : stars)
            if (s.name == targetName)
            {
                // Catalog rows are J2000: precess before the horizontal transform.
                double sRa = s.rightAscensionHours, sDec = s.declinationDegrees;
                PrecessRaDecJ2000ToDate(JulianDate(year, month, day, hour, minute, second, 0.0), sRa, sDec);
                auto [az, alt] = CalculateHorizontalCoordinates(year, month, day, hour, minute, second, 0.0,
                                                                lat, lon, sRa, sDec);
                tAz = az; tAlt = alt; haveTarget = true; break;
            }
        if (!haveTarget)
        {
            double jd = JulianDate(year, month, day, hour, minute, second, 0.0);
            for (auto& p : GetPlanetaryObjects(jd))
                if (p.name == targetName)
                {
                    auto [az, alt] = CalculateHorizontalCoordinates(year, month, day, hour, minute, second, 0.0,
                                                                    lat, lon, p.rightAscensionHours, p.declinationDegrees);
                    tAz = az; tAlt = alt; haveTarget = true; break;
                }
        }
    }

    std::cout << "tracking: " << std::setw(20) << std::left << targetName << std::right;
    if (!haveTarget)
        std::cout << "  (not in catalog)\n";
    else
        std::cout << "  az " << std::fixed << std::setprecision(2) << std::setw(7) << tAz
                  << "  alt " << std::setw(7) << tAlt
                  << (tAlt <= 0 ? "   (below horizon)" : "") << std::defaultfloat << "\n";

    double mAz = 0.0, mAlt = 0.0, age = 0.0;
    if (!ReadMountStatus(mAz, mAlt, age))
    {
        std::cout << "mount:    no position recorded yet - start /autoobservatory "
                  << lat << " " << lon << " --from-planetarium -a --serial DEV\n";
        return;
    }

    std::cout << "mount:    AZ " << std::fixed << std::setprecision(2) << std::setw(7) << mAz
              << "  ALT " << std::setw(7) << mAlt;
    if (haveTarget)
    {
        double dAz = mAz - tAz;
        while (dAz > 180.0) dAz -= 360.0;
        while (dAz < -180.0) dAz += 360.0;
        double dAlt = mAlt - tAlt;
        // true angular separation from the az/alt pair (not Euclidean, which
        // overstates the error near the zenith)
        double sep = std::acos(std::max(-1.0, std::min(1.0,
            std::sin(DegreesToRadians(mAlt)) * std::sin(DegreesToRadians(tAlt)) +
            std::cos(DegreesToRadians(mAlt)) * std::cos(DegreesToRadians(tAlt)) * std::cos(DegreesToRadians(dAz))))) * 180.0 / M_PI;
        std::cout << "   dAZ " << std::showpos << std::setw(6) << dAz
                  << "  dALT " << std::setw(6) << dAlt << std::noshowpos
                  << "  sep " << std::setprecision(2) << sep << " deg"
                  << (sep < 0.5 ? "  ON TARGET" : "");
    }
    if (age > 30.0)
        std::cout << "   (stale " << (int)age << "s)";
    else
        std::cout << "   (" << (int)age << "s ago)";
    std::cout << std::defaultfloat << std::setprecision(6) << "\n";
}

// Map a sky position to grid cells for the current view. Returns false when the
// point is outside the field of view, so the caller can leave it out of the map
// *and* the legend. No horizon test here: the horizon line itself sits at alt 0.
static bool ProjectToCell(double azDeg, double altDeg, int& x, int& y)
{
    double u, v;
    if (!ProjectSky(g_view, azDeg, altDeg, u, v)) return false;
    x = (int)std::round(CX + u * RADIUS * 2.0); // *2: chars twice as tall as wide
    y = (int)std::round(CY - v * RADIUS);       // minus: screen y grows downward
    return true;
}

// Plot an above-horizon object (below-horizon positions are not on the map).
static void Plot(double azDeg, double altDeg, char sym)
{
    if (altDeg <= 0.0) return;
    int x, y;
    if (!ProjectToCell(azDeg, altDeg, x, y)) return; // outside the --fov field
    if (x >= 0 && x < W && y >= 0 && y < H) grid[y][x] = sym;
}

// Draw a two-cell frame around an object so the cursor/target is visible on the
// dome even when the object's own symbol is shared with a neighbour.
static void MarkObject(double azDeg, double altDeg, char left, char right)
{
    if (altDeg <= 0.0) return;
    int x, y;
    if (!ProjectToCell(azDeg, altDeg, x, y)) return;
    if (y < 0 || y >= H) return;
    if (x - 1 >= 0 && x - 1 < W) grid[y][x - 1] = left;
    if (x + 1 >= 0 && x + 1 < W) grid[y][x + 1] = right;
}

// Horizon: the alt = 0 great circle, walked azimuth by azimuth and projected,
// so it is clipped to the field correctly in a limited view. In the full-sky
// view it coincides with the edge circle, exactly as before.
static void DrawHorizon()
{
    const int steps = 1440; // 0.25 deg: enough for a narrow zoomed field
    for (int i = 0; i < steps; ++i)
    {
        int x, y;
        if (!ProjectToCell(360.0 * i / steps, 0.0, x, y)) continue;
        if (x >= 0 && x < W && y >= 0 && y < H) grid[y][x] = '.';
    }
}

// Edge of a limited field: a ring at the drawing radius. Draw only when the
// view is actually limited, otherwise it would overwrite the horizon ring.
static void DrawFieldEdge()
{
    if (!IsLimitedView(g_view)) return;
    const int steps = 400;
    for (int i = 0; i < steps; ++i)
    {
        double t = 2.0 * M_PI * i / steps;
        int x = (int)std::round(CX + RADIUS * std::cos(t) * 2.0);
        int y = (int)std::round(CY - RADIUS * std::sin(t));
        if (x >= 0 && x < W && y >= 0 && y < H) grid[y][x] = '~';
    }
}

// Cardinal labels, nudged just outside the horizon point so they never sit on
// top of the horizon line.
static void DrawCardinals()
{
    const char* labels[] = {"N", "E", "S", "W"};
    for (int k = 0; k < 4; ++k)
    {
        double az = k * 90.0; // N=0 top, E=90 right, S=180 bottom, W=270 left
        int px, py;
        if (!ProjectToCell(az, 0.0, px, py)) continue;
        double dx = px - CX, dy = py - CY;
        double len = std::hypot(dx, dy);
        if (len > 1e-9) { dx /= len; dy /= len; }
        int x = (int)std::round(px + dx * 2.0);
        int y = (int)std::round(py + dy * 1.2);
        if (x >= 0 && x < W && y >= 0 && y < H) grid[y][x] = labels[k][0];
    }
}

// Sun geocentric RA (hours) / Dec (deg) from the low-precision solar formula
// already used for moon phase. ponytail: same model, no extra accuracy needed
// for a header field and a map marker.
static void SunRAandDec(double jd, double& raHours, double& decDeg)
{
    double T = (jd - 2451545.0) / 36525.0;
    double L0 = 280.46646 + 36000.76983 * T;
    double M = 357.52911 + 35999.05029 * T;
    double C = 1.914602 * std::sin(M * M_PI / 180.0);
    double lambda = (L0 + C) * M_PI / 180.0;
    double ra = std::atan2(std::cos(23.4393 * M_PI / 180.0) * std::sin(lambda), std::cos(lambda)) * 12.0 / M_PI;
    if (ra < 0) ra += 24.0;
    raHours = ra;
    decDeg = std::asin(std::sin(23.4393 * M_PI / 180.0) * std::sin(lambda)) * 180.0 / M_PI;
}

// Twilight band from the sun's altitude:
// alt > -0.833 day, > -6 civil, > -12 nautical, > -18 astronomical, else night
static const char* TwilightState(double sunAltDeg)
{
    if (sunAltDeg > -0.833) return "day";
    if (sunAltDeg > -6.0)   return "civil twilight";
    if (sunAltDeg > -12.0)  return "nautical twilight";
    if (sunAltDeg > -18.0)  return "astronomical twilight";
    return "night";
}

static double MoonIllumination(double moonRA, double moonDec, double jd)
{
    // Elongation between Moon and Sun -> illuminated fraction (0=new, 1=full)
    double T = (jd - 2451545.0) / 36525.0;
    double L0 = 280.46646 + 36000.76983 * T;
    double M = 357.52911 + 35999.05029 * T;
    double C = 1.914602 * std::sin(M * M_PI / 180.0);
    double lambda = (L0 + C) * M_PI / 180.0;
    double sunRA = std::atan2(std::cos(23.4393 * M_PI / 180.0) * std::sin(lambda), std::cos(lambda)) * 12.0 / M_PI;
    if (sunRA < 0) sunRA += 24.0;
    double sunDec = std::asin(std::sin(23.4393 * M_PI / 180.0) * std::sin(lambda)) * 180.0 / M_PI;

    double mRA = moonRA * 15.0 * M_PI / 180.0, sRA = sunRA * 15.0 * M_PI / 180.0;
    double mDec = moonDec * M_PI / 180.0, sDec = sunDec * M_PI / 180.0;
    double cosSep = std::sin(mDec) * std::sin(sDec) +
                    std::cos(mDec) * std::cos(sDec) * std::cos(mRA - sRA);
    double sep = std::acos(std::max(-1.0, std::min(1.0, cosSep))) * 180.0 / M_PI;
    return 0.5 * (1.0 - std::cos(sep * M_PI / 180.0));
}

// Phase name from the Moon-Sun elongation direction: is the Moon east (waxing)
// or west (waning) of the Sun in ecliptic longitude?
static const char* MoonPhaseName(double moonRA, double moonDec, double jd, double illum)
{
    double T = (jd - 2451545.0) / 36525.0;
    double L0 = 280.46646 + 36000.76983 * T;
    double M = 357.52911 + 35999.05029 * T;
    double C = 1.914602 * std::sin(M * M_PI / 180.0);
    double lambdaSun = std::fmod(L0 + C, 360.0); // normalize before the elongation difference
    double raR = moonRA * 15.0 * M_PI / 180.0, decR = moonDec * M_PI / 180.0, obl = 23.4393 * M_PI / 180.0;
    double x = std::cos(decR) * std::cos(raR);
    double y = std::cos(decR) * std::sin(raR);
    double z = std::sin(decR);
    double yE = y * std::cos(obl) - z * std::sin(obl); // inverse of the forward ecl->equ used by the model
    double xE = x;
    double lambdaMoon = std::atan2(yE, xE) * 180.0 / M_PI; // (-180,180]
    lambdaSun = std::fmod(lambdaSun + 180.0, 360.0) - 180.0; // same range as lambdaMoon
    double elong = lambdaMoon - lambdaSun;              // + = east of sun = waxing
    if (elong < -180) elong += 360;
    if (elong > 180) elong -= 360;
    bool waxing = elong > 0;

    if (illum < 0.03)  return "New Moon";
    if (illum > 0.97)  return "Full Moon";
    if (std::fabs(elong) > 80 && std::fabs(elong) < 100)
        return waxing ? "First Quarter" : "Last Quarter";
    if (waxing) return illum < 0.5 ? "Waxing Crescent" : "Waxing Gibbous";
    return illum < 0.5 ? "Waning Crescent" : "Waning Gibbous";
}

// Find moonrise/moonset (UTC hours) by scanning altitude in 5-min steps over the
// requested UTC day. Sets rise/set to -1 when the event doesn't occur that day.
// ponytail: brute-force scan, not a rise/set equation solver; 5-min granularity
// is plenty for a display line. Switch to interpolation if minute precision matters.
static void FindMoonRiseSet(double year, double month, double day,
                            double lat, double lon, double& rise, double& set)
{
    rise = -1.0; set = -1.0;
    double prevAlt = 0.0;
    bool first = true;
    for (double t = 5.0 / 60.0; t <= 24.0; t += 5.0 / 60.0)
    {
        auto [ra, dec, mag] = ComputeMoonGeocentric(JulianDate(year, month, day, 0.0, 0.0, 0.0, 0.0) + t / 24.0);
        auto [az, alt] = CalculateHorizontalCoordinates(year, month, day, t, 0.0, 0.0, 0.0, lat, lon, ra, dec);
        (void)az;
        if (!first)
        {
            if (prevAlt < 0 && alt >= 0 && rise < 0) rise = t;
            if (prevAlt > 0 && alt <= 0 && set < 0) set = t;
        }
        prevAlt = alt;
        first = false;
    }
}

// Generic rise/set scan against a fixed-threshold altitude (sun uses -0.833
// with refraction/semidiameter, civil twilight uses -6). Reuses the same
// 5-min brute force as the moon. passRAandDec supplies the object's position
// at each step; the threshold crossings are found the same way.
static void FindSunEvents(double year, double month, double day,
                          double lat, double lon, double thresholdDeg,
                          double& rise, double& set)
{
    rise = -1.0; set = -1.0;
    double prevAlt = 0.0;
    bool first = true;
    for (double t = 5.0 / 60.0; t <= 24.0; t += 5.0 / 60.0)
    {
        double ra, dec;
        SunRAandDec(JulianDate(year, month, day, 0.0, 0.0, 0.0, 0.0) + t / 24.0, ra, dec);
        auto [az, alt] = CalculateHorizontalCoordinates(year, month, day, t, 0.0, 0.0, 0.0, lat, lon, ra, dec);
        (void)az;
        if (!first)
        {
            if (prevAlt < thresholdDeg && alt >= thresholdDeg && rise < 0) rise = t;
            if (prevAlt > thresholdDeg && alt <= thresholdDeg && set < 0) set = t;
        }
        prevAlt = alt;
        first = false;
    }
}

static void RenderFrame(double lat, double lon, double magLimit,
                        double year, double month, double day,
                        double hour, double minute, double second)
{
    // Brightest objects get letters; rest get dots.
    // Build the catalog positions BEFORE drawing anything: follow mode needs
    // the locked target's az/alt to recentre the view, and 'sky' is the one
    // place every star and planet's position is known regardless of mag limit
    // or where the current field happens to point.
    std::vector<SkyObject> sky;
    std::vector<CelestialObject> stars;
    try { stars = CelestialObjectReader::ReadFromCSV("celestial_objects.csv"); }
    catch (...) { std::cerr << "cannot read celestial_objects.csv\n"; std::exit(1); } // no CSV, no map

    // Shorthand on the dome: letters for the brightest stars (A, B, C...),
    // digits for the brightest star clusters (1, 2, 3...). The legend maps
    // every symbol to its full name.
    auto isCluster = [](const std::string& n) {
        return n.find("Cluster") != std::string::npos || n.find("Pleiades") != std::string::npos
            || n.find("Hyades") != std::string::npos || n.find("Beehive") != std::string::npos;
    };
    char nextSym = 'A';
    int nextCluster = 1;
    double starJD = JulianDate(year, month, day, hour, minute, second, 0.0);
    for (auto& s : stars)
    {
        // Catalog rows are J2000: precess before the horizontal transform.
        double sRa = s.rightAscensionHours, sDec = s.declinationDegrees;
        PrecessRaDecJ2000ToDate(starJD, sRa, sDec);
        auto [az, alt] = CalculateHorizontalCoordinates(year, month, day, hour, minute, second, 0.0,
                                                        lat, lon, sRa, sDec);
        char sym = '.';
        if (s.magnitude <= 1.5 && alt > 0)
        {
            if (isCluster(s.name))
            {
                // digits for clusters; after 9, reuse lowercase letters a..z
                sym = nextCluster <= 9 ? (char)('0' + nextCluster) : (char)('a' + nextCluster - 10);
                ++nextCluster;
            }
            else
            {
                sym = nextSym++;
                if (nextSym > 'Z') nextSym = 'a'; // run out of caps, use lowercase
            }
        }
        sky.push_back({s.name, az, alt, s.magnitude, false, sym, s.bayer});
    }

    double jd = JulianDate(year, month, day, hour, minute, second, 0.0);
    auto planets = GetPlanetaryObjects(jd);
    double moonRA = 0, moonDec = 0;
    for (auto& p : planets)
    {
        if (p.name == "Moon") { moonRA = p.rightAscensionHours; moonDec = p.declinationDegrees; }
        auto [az, alt] = CalculateHorizontalCoordinates(year, month, day, hour, minute, second, 0.0,
                                                        lat, lon, p.rightAscensionHours, p.declinationDegrees);
        char sym = p.name == "Moon" ? 'O' : (p.name[0] | 0x20); // Moon=O, else lowercase initial
        sky.push_back({p.name, az, alt, p.magnitude, true, sym});
    }

    // Follow mode: recentre the limited view on the locked target every frame,
    // so the field tracks it as the sky turns. 'sky' holds every catalog
    // object, so a target fainter than the mag limit (or currently outside the
    // field) still steers the view. Manual pan keys turn follow back off.
    if (g_follow && !g_targetName.empty())
    {
        for (auto& o : sky)
            if (o.name == g_targetName)
            {
                g_view.centerAz = NormalizeDegrees(o.az);
                g_view.centerAlt = o.alt;
                break;
            }
    }

    for (auto& row : grid) for (auto& c : row) c = ' ';
    DrawHorizon();
    DrawFieldEdge();
    DrawCardinals();
    { // zenith marker: only when it falls inside the current view
        int zx, zy;
        if (ProjectToCell(0.0, 90.0, zx, zy) && zx >= 0 && zx < W && zy >= 0 && zy < H)
            grid[zy][zx] = '+';
    }

    // Sun: computed once, drawn above horizon, reported in the header
    double sunRA, sunDec;
    SunRAandDec(jd, sunRA, sunDec);
    auto [sunAz, sunAlt] = CalculateHorizontalCoordinates(year, month, day, hour, minute, second, 0.0,
                                                          lat, lon, sunRA, sunDec);
    bool sunUp = sunAlt > 0;
    if (sunUp) Plot(sunAz, sunAlt, '@');

    // Plot: brighter above-horizon objects win over fainter at same cell
    std::sort(sky.begin(), sky.end(), [](const SkyObject& a, const SkyObject& b) {
        return (a.alt > 0 ? a.mag : 99) < (b.alt > 0 ? b.mag : 99);
    });
    std::vector<SkyObject> plotted; // keep legend order = brightest first
    for (auto& o : sky)
    {
        if (o.alt <= 0.0) continue;              // below the horizon
        if (o.mag > magLimit) continue;          // fainter than the limit
        int x, y;
        if (!ProjectToCell(o.az, o.alt, x, y)) continue; // outside the --fov field
        if (x >= 0 && x < W && y >= 0 && y < H) grid[y][x] = o.sym;
        plotted.push_back(o);
    }
    g_plotted = plotted; // selectable list for the live-mode target picker

    // cursor = ( ), locked target = [ ]
    for (auto& o : g_plotted)
    {
        if (!g_cursorName.empty() && o.name == g_cursorName)
            MarkObject(o.az, o.alt, '(', ')');
        if (!g_targetName.empty() && o.name == g_targetName)
            MarkObject(o.az, o.alt, '[', ']');
    }

    std::cout << "SKY MAP - looking UP  |  ";
    std::cout << (int)year << "-" << std::setfill('0') << std::setw(2) << (int)month
              << "-" << std::setw(2) << (int)day << " " << std::setw(2) << (int)hour << ":"
              << std::setw(2) << (int)minute << ":" << std::setw(2) << (int)second << " UTC"
              << std::setfill(' ') << "  |  ";
    std::cout << lat << "N " << lon << "E  |  moon ~"
              << (int)std::round(MoonIllumination(moonRA, moonDec, jd) * 100) << "% "
              << MoonPhaseName(moonRA, moonDec, jd, MoonIllumination(moonRA, moonDec, jd)) << "\n";
    std::cout << "sun alt " << std::fixed << std::setprecision(1) << sunAlt << " deg  |  "
              << TwilightState(sunAlt) << std::defaultfloat << std::setprecision(6) << "\n";
    auto hhmm = [](double t) {
        int h = (int)t, m = (int)std::round((t - h) * 60);
        if (m == 60) { m = 0; ++h; }
        std::ostringstream o; o << std::setfill('0') << std::setw(2) << h << ":" << std::setw(2) << m; return o.str();
    };
    {
        double rise = -1.0, set = -1.0;
        FindSunEvents(year, month, day, lat, lon, -0.833, rise, set);
        double civilRise = -1.0, civilSet = -1.0;
        FindSunEvents(year, month, day, lat, lon, -6.0, civilRise, civilSet);
        std::cout << "sun ";
        if (rise >= 0)  std::cout << "rise " << hhmm(rise) << " UTC";
        if (set >= 0)   std::cout << (rise >= 0 ? "  " : "") << "set " << hhmm(set) << " UTC";
        if (rise < 0 && set < 0) std::cout << (sunAlt > 0 ? "up all day" : "down all day");
        if (civilRise >= 0 || civilSet >= 0)
        {
            std::cout << "  |  civil ";
            if (civilRise >= 0) std::cout << hhmm(civilRise);
            if (civilSet >= 0)  std::cout << (civilRise >= 0 ? "-" : "") << hhmm(civilSet);
            std::cout << " UTC";
        }
        std::cout << "\n";
    }
    {
        double rise = -1.0, set = -1.0;
        FindMoonRiseSet(year, month, day, lat, lon, rise, set);
        if (rise >= 0 || set >= 0)
        {
            std::cout << "Moon ";
            if (rise >= 0) std::cout << "rise " << hhmm(rise) << " UTC";
            if (set >= 0)  std::cout << (rise >= 0 ? "  " : "") << "set " << hhmm(set) << " UTC";
            std::cout << "\n";
        }
    }
    // live target + mount pointing panel (only when a target is selected)
    PrintTrackingPanel(g_targetName, lat, lon, year, month, day, hour, minute, second);

    // what slice of the sky the --fov/--center (or the live zoom keys) show,
    // and whether follow mode is steering it at the locked target
    std::cout << "view: center az " << std::fixed << std::setprecision(1) << g_view.centerAz
              << " alt " << g_view.centerAlt << "  fov " << g_view.fov << " deg  "
              << (IsLimitedView(g_view) ? "(limited)" : "(full sky)")
              << "  follow: " << FollowState()
              << std::defaultfloat << std::setprecision(6) << "\n";

    if (IsLimitedView(g_view))
        std::cout << "limited view: '~' = field edge, '.' = horizon, N top / E right / S bottom / W left\n\n";
    else
        std::cout << "center = zenith (straight up), circle = horizon, N top / E right / S bottom / W left\n\n";

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x) std::cout << grid[y][x];
        std::cout << "\n";
    }

    std::cout << "\nLegend (brightest first):\n";
    for (auto& o : plotted)
    {
        if (o.sym == '.') continue; // faint unnamed dots: skip in legend
        char flag = ' ';
        if (!g_targetName.empty() && o.name == g_targetName) flag = '*';
        else if (!g_cursorName.empty() && o.name == g_cursorName) flag = '>';
        // Stars carry their Bayer designation (e.g. Alpha_Boo Arcturus) for a
        // more professional map; clusters keep the digit shorthand (1 C Hyades).
        bool isClusterSym = o.sym >= '0' && o.sym <= '9';
        std::string label;
        if (isClusterSym)
            label = "C " + o.name;
        else if (!o.bayer.empty())
            label = o.bayer + " " + o.name;
        else
            label = o.name;
        // Wider layout for 100-col terminals: name column gets more room so
        // Bayer designations don't push the az/alt/mag data off the side.
        std::cout << flag << " " << o.sym << " " << std::left << std::setw(44) << label
                  << "az " << std::setw(7) << std::fixed << std::setprecision(1) << o.az
                  << "alt " << std::setw(7) << o.alt
                  << "mag " << std::setw(7) << o.mag
                  << (o.isPlanet ? "  (planet)" : "") << "\n";
    }
    std::cout << std::defaultfloat << std::setprecision(6);
    std::cout << "  . = faint;  B = Bayer, C = cluster, O = Moon, @ = sun, + = zenith";
    if (IsLimitedView(g_view)) std::cout << ", ~ = field edge";
    std::cout << "\n";
    std::cout << "  ( ) = cursor, [ ] = target  (mag limit " << magLimit << ", fov " << g_view.fov << ")\n";
}

int main(int argc, char* argv[])
{
    double lat = 45.0, lon = 10.0;
    double magLimit = 4.0;
    std::string dateStr, timeStr, selectName;
    bool live = false;
    double interval = 10.0;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "-h" || a == "--help")
        {
            std::cout <<
                "Usage: planetarium [lat lon] [options]\n"
                "Draws a planisphere-style ASCII sky map for the given observer and time.\n\n"
                "  lat lon            Observer latitude/longitude (default 45 10): decimal\n"
                "                     degrees (45.5 -10.25) or DEG MIN SEC HEMI, e.g.\n"
                "                     45 30 15 N 9 12 30 E; S/W are negative.\n"
                "  --date YYYY-MM-DD  Date in UTC (default: today).\n"
                "  --time HH:MM[:SS]  Time in UTC (default: now).\n"
                "  --mag N            Magnitude limit, lower = fewer objects (default 4.0).\n"
                "  --fov DEG          Angular DIAMETER of the circular field, in degrees;\n"
                "                     zooms in on one part of the sky. Must be smaller than\n"
                "                     a full sky: 0 < fov < 180. Omit it for the whole sky.\n"
                "  --center AZ,ALT    Centre of a limited view: azimuth 0-360 (0 = N, 90 = E)\n"
                "                     and altitude -90..90 (90 = zenith). Also accepts two\n"
                "                     tokens (--center 210 45). Default: 0,90 (zenith).\n"
                "  --follow           Keep the view centred on the --select target as the\n"
                "                     sky turns (live key: f toggles it). Does nothing\n"
                "                     without a target.\n"
                "  --select NAME      Set the mount target without interaction (writes\n"
                "                     selected_target.txt; case-insensitive, resolves a\n"
                "                     unique prefix/substring like \"arctur\" -> Arcturus,\n"
                "                     and lists candidates instead of guessing if ambiguous).\n"
                "  --live [seconds]   Continuously redraw from the real clock every N seconds\n"
                "                     (default 10). Ignores --date/--time.\n"
                "  -h, --help         Show this help.\n\n"
                "Exit codes: 0 success, 1 runtime error (e.g. unknown --select name),\n"
                "            2 usage error (bad number, unknown option, bad lat/lon).\n\n"
                "In live mode: n/p (or Tab) cycle the object cursor, t locks the high-\n"
                "lighted object as the mount target, c clears it, q quits. The target\n"
                "name is written to selected_target.txt, which\n"
                "./autoobservatory <lat> <lon> --from-planetarium -a --serial DEV reads.\n\n"
                "In live mode the field of view is adjustable: +/- zoom in/out, w/a/s/d pan\n"
                "the centre (w = toward zenith), f follows the locked target, 0 returns to\n"
                "the full sky. Panning stops follow. Arrow keys and n/p/t/c keep their\n"
                "meanings.\n\n"
                "The map is the dome as seen looking UP: zenith (+) at center, the circle is\n"
                "the horizon, N top / E right / S bottom / W left. Objects farther from the\n"
                "center are lower in the sky; below-horizon objects are not drawn. With\n"
                "--fov the circle instead is the edge of the selected field ('~'), and\n"
                "only objects inside it are drawn. Letters\n"
                "mark the brightest stars, digits the brightest clusters (legend below the\n"
                "map); O is the Moon, planets use their lowercase initial. Requires\n"
                "celestial_objects.csv and planetary_objects.csv in the working directory.\n";
            return 0;
        }
        else if (a == "--date" && i + 1 < argc)      { dateStr = argv[++i]; }
        else if (a == "--time" && i + 1 < argc) { timeStr = argv[++i]; }
        else if (a == "--mag" && i + 1 < argc)  { magLimit = ParseDoubleArg(argv[++i], "magnitude limit"); }
        else if (a == "--fov" && i + 1 < argc)
        {
            g_view.fov = ParseDoubleArg(argv[++i], "field of view");
            if (!ValidFov(g_view.fov))
            {
                std::cerr << "Error: --fov must be smaller than a full sky "
                             "(0 < fov < 180 degrees, got " << g_view.fov << ")\n";
                return 2;
            }
        }
        else if (a == "--center" && i + 1 < argc)
        {
            std::string spec = argv[++i];
            double cAz = 0.0, cAlt = 0.0;
            size_t comma = spec.find(',');
            if (comma != std::string::npos && spec.find(',', comma + 1) != std::string::npos)
            {
                std::cerr << "Error: --center takes AZ,ALT (one comma), got \"" << spec << "\"\n";
                return 2;
            }
            if (comma != std::string::npos)
            {
                cAz  = ParseDoubleArg(spec.substr(0, comma).c_str(), "view centre azimuth");
                cAlt = ParseDoubleArg(spec.substr(comma + 1).c_str(), "view centre altitude");
            }
            else if (i + 1 < argc)
            {
                cAz  = ParseDoubleArg(spec.c_str(), "view centre azimuth");
                cAlt = ParseDoubleArg(argv[++i], "view centre altitude");
            }
            else
            {
                std::cerr << "Error: --center needs AZ,ALT (or AZ ALT), e.g. --center 210,45\n";
                return 2;
            }
            if (!ValidCenter(cAz, cAlt))
            {
                std::cerr << "Error: --center needs azimuth in [0, 360) and altitude in "
                             "[-90, 90] (got " << cAz << ", " << cAlt << ")\n";
                return 2;
            }
            g_view.centerAz = cAz;
            g_view.centerAlt = cAlt;
        }
        else if (a == "--follow") { g_follow = true; }
        else if (a == "--live")
        {
            live = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                interval = ParseDoubleArg(argv[++i], "refresh interval");
        }
        else if (a == "--select" && i + 1 < argc) { selectName = argv[++i]; }
        // Catch unrecognized/incomplete options instead of silently ignoring them.
        // Negative coordinates ("-33.9") start with '-' too, so only reject a '-'
        // that is not a digit or decimal point.
        else if (a.size() >= 2 && a[0] == '-' &&
                 !std::isdigit((unsigned char)a[1]) && a[1] != '.')
        {
            std::cerr << "Error: unknown or incomplete option \"" << a << "\"\n";
            return 2;
        }
        else
        {
            // Collect the positional coordinate group (DMS or decimal) - it may
            // span 2..8 tokens, so gather until the next option-looking token.
            std::vector<std::string> tokens;
            int j = i;
            while (j < argc)
            {
                std::string s = argv[j];
                if (s.size() >= 2 && s[0] == '-' &&
                    !std::isdigit((unsigned char)s[1]) && s[1] != '.')
                    break; // next option starts here
                tokens.push_back(s);
                ++j;
            }
            ParsePositionalCoords(tokens, lat, lon);
            i = j - 1;
        }
    }

    // --select NAME: set the mount target without any interaction. Works with
    // and without --live, so scripts can do: planetarium --select Vega &&
    // autoobservatory ... --from-planetarium -a
    if (!selectName.empty())
    {
        std::string canonical;
        std::vector<std::string> candidates;
        if (!ResolveObjectName(LoadCatalogNames(), selectName, canonical, candidates))
        {
            ReportNameFailure(selectName, candidates);
            return 1;
        }
        g_targetName = canonical;
        g_cursorName = canonical;
        WriteTargetFile(canonical);
        std::cout << "Target set: " << canonical;
        if (!CaseInsensitiveEq(canonical, selectName))
            std::cout << "  (fuzzy match for \"" << selectName << "\")";
        std::cout << "  (written to " << TARGET_FILE << ")\n";
    }

    if (live)
    {
        // ponytail live loop: real clock each tick, full clear+redraw (no frame
        // diffing), fractional sleep in 10 ms steps. Press q to quit (Ctrl-C
        // also works via the same restore path).
        // Raw-ish terminal: disable echo/canonical so q is seen instantly,
        // non-blocking stdin so the sleep loop isn't interrupted.
        termios orig{};
        tcgetattr(STDIN_FILENO, &orig);
        termios raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        auto restore = [&]() {
            tcsetattr(STDIN_FILENO, TCSANOW, &orig);
            fcntl(STDIN_FILENO, F_SETFL, flags);
            std::cout << "\033[0m";
        };
        std::signal(SIGINT, [](int) { std::signal(SIGINT, SIG_DFL); std::raise(SIGINT); });
        bool quit = false;
        bool redraw = false; // arrow keys bump settings -> redraw immediately
        // Read one key; returns 0, or 1001/1002/1003/1004 for up/down/right/left
        auto getKey = []() {
            char c;
            if (read(STDIN_FILENO, &c, 1) != 1) return 0;
            if (c == '\033')
            {
                char seq[2];
                if (read(STDIN_FILENO, &seq[0], 1) != 1 || seq[0] != '[') return 0;
                if (read(STDIN_FILENO, &seq[1], 1) != 1) return 0;
                switch (seq[1])
                {
                    case 'A': return 1001; // up
                    case 'B': return 1002; // down
                    case 'C': return 1003; // right
                    case 'D': return 1004; // left
                }
                return 0;
            }
            return (int)(unsigned char)c;
        };
        while (!quit)
        {
            std::time_t now = std::time(nullptr);
            std::tm* utc = std::gmtime(&now);
            std::cout << "\033[H\033[J";
            RenderFrame(lat, lon, magLimit, utc->tm_year + 1900.0, utc->tm_mon + 1.0,
                        utc->tm_mday + 0.0, utc->tm_hour + 0.0, utc->tm_min + 0.0, utc->tm_sec + 0.0);
            std::cout << "\n[<-/->] interval " << interval << "s   [up/down] mag "
                      << std::fixed << std::setprecision(1) << magLimit
                      << std::defaultfloat << std::setprecision(6) // restore for the next frame
                      << "   [n/p] cycle   [t] target   [c] clear   [q] quit\n";
            std::cout << "view az " << std::fixed << std::setprecision(1) << g_view.centerAz
                      << " alt " << g_view.centerAlt << "  fov " << g_view.fov << " deg"
                      << (IsLimitedView(g_view) ? "  (limited)" : "  (full sky)")
                      << std::defaultfloat << std::setprecision(6)
                      << "   [f] follow " << FollowState()
                      << "   [+/-] zoom   [w/a/s/d] pan   [0] full sky\n";
            if (!g_targetName.empty())
            {
                std::ostringstream cmd;
                cmd << "./autoobservatory " << std::fixed << std::setprecision(4) << lat << " " << lon
                    << " --from-planetarium -a --serial /dev/ttyUSB0";
                std::cout << "target: \"" << g_targetName << "\"  ->  " << cmd.str() << "\n";
                std::cout << std::defaultfloat;
            }
            else if (!g_cursorName.empty())
                std::cout << "cursor: " << g_cursorName << "   (press t to make it the mount target)\n";
            std::cout << std::flush;
            redraw = false;
            for (double t = 0; t < interval && !quit && !redraw; t += 0.02)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                int key;
                while ((key = getKey()) != 0)
                {
                    if (key == 'q' || key == 'Q') { quit = true; break; }
                    // up/down: mag limit +-0.5, clamped to [-1, 7]
                    if (key == 1001) magLimit = std::min(10.0, magLimit + 0.5);
                    if (key == 1002) magLimit = std::max(-1.0, magLimit - 0.5);
                    // left/right: refresh interval x / 2, clamped to [0.25, 300]
                    if (key == 1003) interval = std::min(300.0, interval * 2.0);
                    if (key == 1004) interval = std::max(0.25, interval / 2.0);
                    if (key >= 1001 && key <= 1004) redraw = true; // apply at once

                    // view: +/- zoom, w/a/s/d pan the centre, 0 = full sky.
                    // Panning or resetting is a manual override, so it drops
                    // follow mode; zooming keeps tracking the locked target.
                    if (key == '+' || key == '=') { g_view.fov = std::max(1.0, g_view.fov / 1.5); redraw = true; }
                    if (key == '-' || key == '_') { g_view.fov = std::min(180.0, g_view.fov * 1.5); redraw = true; }
                    if (key == 'f' || key == 'F') { g_follow = !g_follow; redraw = true; }
                    if (key == 'w' || key == 'W') { g_view.centerAlt = std::min(90.0, g_view.centerAlt + g_view.fov / 4.0); g_follow = false; redraw = true; }
                    if (key == 's' || key == 'S') { g_view.centerAlt = std::max(-90.0, g_view.centerAlt - g_view.fov / 4.0); g_follow = false; redraw = true; }
                    if (key == 'a' || key == 'A') { g_view.centerAz = NormalizeDegrees(g_view.centerAz - g_view.fov / 4.0); g_follow = false; redraw = true; }
                    if (key == 'd' || key == 'D') { g_view.centerAz = NormalizeDegrees(g_view.centerAz + g_view.fov / 4.0); g_follow = false; redraw = true; }
                    if (key == '0') { g_view = FullSkyView(); g_follow = false; redraw = true; }

                    // target picker: cycle the cursor, lock it, or clear it
                    bool cycleNext = (key == 'n' || key == 'N' || key == '\t');
                    bool cyclePrev = (key == 'p' || key == 'P');
                    if ((cycleNext || cyclePrev) && !g_plotted.empty())
                    {
                        int cur = -1;
                        for (int i = 0; i < (int)g_plotted.size(); ++i)
                            if (g_plotted[i].name == g_cursorName) { cur = i; break; }
                        int n = (int)g_plotted.size();
                        int next = cycleNext ? (cur + 1) % n : (cur <= 0 ? n - 1 : cur - 1);
                        g_cursorName = g_plotted[next].name;
                        redraw = true;
                    }
                    if (key == 't' || key == 'T' || key == '\r' || key == '\n')
                    {
                        if (g_cursorName.empty() && !g_plotted.empty())
                            g_cursorName = g_plotted.front().name;
                        if (!g_cursorName.empty())
                        {
                            g_targetName = g_cursorName;
                            WriteTargetFile(g_targetName);
                            redraw = true;
                        }
                    }
                    if (key == 'c' || key == 'C')
                    {
                        g_targetName.clear();
                        WriteTargetFile("");
                        redraw = true;
                    }
                }
            }
        }
        restore();
        std::cout << "Planetarium closed.\n";
        return 0;
    }

    std::time_t now = std::time(nullptr);
    std::tm* utc = std::gmtime(&now);
    double year = utc->tm_year + 1900, month = utc->tm_mon + 1, day = utc->tm_mday;
    double hour = utc->tm_hour, minute = utc->tm_min, second = utc->tm_sec;

    if (!dateStr.empty())
        ParseDateArg(dateStr, year, month, day);
    if (!timeStr.empty())
        ParseTimeArg(timeStr, hour, minute, second);

    RenderFrame(lat, lon, magLimit, year, month, day, hour, minute, second);
    return 0;
}
