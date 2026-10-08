#ifndef CLI_PARSE_H
#define CLI_PARSE_H

// cli_parse.h - small shared argument helpers for the three binaries:
// numeric parsing that prints a clean error instead of std::stod/std::stoi's
// uncaught exception (which calls std::terminate and aborts with exit 134),
// and an observer latitude/longitude range check. Keeps the argument errors
// consistent across planetarium, autoobservatory and calibrate.
//
// Exit-code convention (all binaries): 0 = success, 1 = runtime error
// (serial device, missing files, unresolvable object), 2 = usage error
// (bad number, unknown option, impossible observer position). These helpers
// therefore exit 2: the problem is in how the program was invoked.

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

inline double ParseDoubleArg(const char* s, const char* what)
{
    try { return std::stod(s); }
    catch (...) { std::cerr << "Error: invalid " << what << " \"" << s << "\"\n"; std::exit(2); }
}

inline int ParseIntArg(const char* s, const char* what)
{
    try { return std::stoi(s); }
    catch (...) { std::cerr << "Error: invalid " << what << " \"" << s << "\"\n"; std::exit(2); }
}

// Exits with 2 (usage error) when the observer position is impossible.
inline void ValidObserver(double lat, double lon)
{
    if (lat < -90.0 || lat > 90.0)
    {
        std::cerr << "Error: latitude must be between -90 and 90 (got " << lat << ")\n";
        std::exit(2);
    }
    if (lon < -180.0 || lon > 180.0)
    {
        std::cerr << "Error: longitude must be between -180 and 180 (got " << lon << ")\n";
        std::exit(2);
    }
}

// --- --date/--time parsing ---------------------------------------------------
// Strict parsing of the documented formats (YYYY-MM-DD, HH:MM[:SS]). The old
// stringstream extraction silently accepted garbage (--date abc became year 0,
// --time 25:99 was used verbatim), which produces silently wrong sky maps.
// These helpers exit 2 (usage error) on anything malformed or out of range.

inline bool LeapYear(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

inline int DaysInMonth(int y, int m)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && LeapYear(y) ? 29 : d[m - 1];
}

// "YYYY-MM-DD" -> y, m, d. Exits 2 on malformed or out-of-range input.
inline void ParseDateArg(const std::string& s, double& y, double& m, double& d)
{
    int Y = 0, M = 0, D = 0;
    char trail = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d%c", &Y, &M, &D, &trail) != 3 || s.size() != 10)
    {
        std::cerr << "Error: invalid --date \"" << s << "\"; expected YYYY-MM-DD\n";
        std::exit(2);
    }
    if (Y < 1 || Y > 9999 || M < 1 || M > 12 || D < 1 || D > DaysInMonth(Y, M))
    {
        std::cerr << "Error: --date \"" << s << "\" is not a real calendar date\n";
        std::exit(2);
    }
    y = Y; m = M; d = D;
}

// "HH:MM" or "HH:MM:SS" -> h, mi, sec. Exits 2 on malformed or out-of-range input.
inline void ParseTimeArg(const std::string& s, double& h, double& mi, double& sec)
{
    int H = 0, M = 0, S = 0;
    char trail = 0;
    int n = std::sscanf(s.c_str(), "%d:%d:%d%c", &H, &M, &S, &trail);
    if ((n != 2 && n != 3) || (n == 2 && s.size() != 5) || (n == 3 && s.size() != 8))
    {
        std::cerr << "Error: invalid --time \"" << s << "\"; expected HH:MM or HH:MM:SS\n";
        std::exit(2);
    }
    if (H < 0 || H > 23 || M < 0 || M > 59 || (n == 3 && (S < 0 || S > 59)))
    {
        std::cerr << "Error: --time \"" << s << "\" is out of range (HH 0-23, MM/SS 0-59)\n";
        std::exit(2);
    }
    h = H; mi = M; sec = n == 3 ? S : 0;
}

// --- coordinate parsing ------------------------------------------------------
// Positional lat/lon accept decimal degrees ("45.5 10.25") or DEG MIN SEC HEMI
// ("45 30 15 N 9 12 30 E"). Per coordinate the accepted forms are:
//   D            decimal degrees
//   D H          decimal degrees + hemisphere (45.5 N)
//   D M H        degrees + (fractional) arcminutes + hemisphere
//   D M S H      degrees + arcminutes + (fractional) arcseconds + hemisphere
// H is one of N/S (latitude, S negative) or E/W (longitude, W negative).
// Everything is a usage error (exit 2) when malformed.

inline bool IsHemisphere(const std::string& s)
{
    return s == "N" || s == "S" || s == "E" || s == "W" ||
           s == "n" || s == "s" || s == "e" || s == "w";
}

inline bool LooksNumeric(const std::string& s)
{
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end && *end == '\0' && end != s.c_str();
}

// Consumes one coordinate from tokens starting at *idx, advancing it past what
// was used. `isLat` selects the allowed hemisphere letters.
inline double ParseCoordTokens(const std::vector<std::string>& t, size_t& idx, bool isLat)
{
    const char* axis = isLat ? "latitude" : "longitude";
    if (idx >= t.size())
    {
        std::cerr << "Error: missing " << axis << "\n";
        std::exit(2);
    }

    double deg = ParseDoubleArg(t[idx].c_str(), axis);
    ++idx;

    // A following numeric token means D M (or D M S) - but only when a
    // hemisphere letter terminates the group. Without that lookahead,
    // "planetarium 45 10" (lat 45, lon 10) would misread as 45deg10'.
    bool hasMin = idx < t.size() && LooksNumeric(t[idx]);
    bool hemiAhead = hasMin &&
        ((idx + 1 < t.size() && IsHemisphere(t[idx + 1])) ||
         (idx + 2 < t.size() && LooksNumeric(t[idx + 1]) && IsHemisphere(t[idx + 2])));
    if (hemiAhead)
    {
        double min = ParseDoubleArg(t[idx].c_str(), "arcminutes");
        if (min < 0.0 || min >= 60.0)
        {
            std::cerr << "Error: " << axis << " arcminutes must be in [0, 60) (got " << min << ")\n";
            std::exit(2);
        }
        deg += min / 60.0;
        ++idx;
        // optional arcseconds
        if (idx < t.size() && LooksNumeric(t[idx]))
        {
            double sec = ParseDoubleArg(t[idx].c_str(), "arcseconds");
            if (sec < 0.0 || sec >= 60.0)
            {
                std::cerr << "Error: " << axis << " arcseconds must be in [0, 60) (got " << sec << ")\n";
                std::exit(2);
            }
            deg += sec / 3600.0;
            ++idx;
        }
    }

    if (idx >= t.size() || !IsHemisphere(t[idx]))
        return deg; // plain decimal degrees, no hemisphere

    char h = (char)std::toupper(t[idx][0]);
    ++idx;
    bool hOk = isLat ? (h == 'N' || h == 'S') : (h == 'E' || h == 'W');
    if (!hOk)
    {
        std::cerr << "Error: hemisphere \"" << h << "\" is not valid for " << axis << "\n";
        std::exit(2);
    }
    bool negative = (h == 'S' || h == 'W');
    // With an explicit hemisphere the magnitude wins ("45 30 S" = -45.5).
    return negative ? -std::fabs(deg) : std::fabs(deg);
}

// Parses the full positional coordinate list: latitude then longitude.
// Exits 2 when tokens are left over or a coordinate is malformed.
inline void ParsePositionalCoords(const std::vector<std::string>& t, double& lat, double& lon)
{
    size_t idx = 0;
    lat = ParseCoordTokens(t, idx, true);
    lon = ParseCoordTokens(t, idx, false);
    if (idx != t.size())
    {
        std::cerr << "Error: unexpected extra argument \"" << t[idx] << "\" after coordinates\n";
        std::exit(2);
    }
    ValidObserver(lat, lon);
}

#endif
