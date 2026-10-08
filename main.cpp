#include <iostream>
#include <iomanip>
#include <fstream>
#include <cmath>
// (fstream also backs the mount_status.txt hand-off written below)
#include <ctime>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include "celestial_objects.h"
#include "celestial_object_reader.h"
#include "planetary_objects.h"
#include "wmm_mag.h"
#include "astro_math.h"
#include "mount_control.h"
#include "terminal_io.h"
#include "object_name.h"
#include "cli_parse.h"

void DisplayResults(double observerLat, double observerLon, bool visibleOnly, std::string targetName,
                    double year, double month, double day, double hour, double minute, double second, double millisecond,
                    double magneticDeclination, bool magneticMode);
void DisplayArduinoResults(double observerLat, double observerLon, std::string targetName,
                           double year, double month, double day, double hour, double minute, double second, double millisecond,
                           double magneticDeclination, bool magneticMode,
                           SlewRateLimiter& azLimiter, SlewRateLimiter& altLimiter, double dtSec,                            int serialFd, double deadzoneDeg, LastSent& lastSent, bool dryRun);

class MagneticDeclination {
public:
    bool initialize(const char* cof_path = "WMM2025.COF") {
        initialized = wmm_init(cof_path) != 0;
        return initialized;
    }

    double getDeclination(double lat, double lon, double altKm, double decimalYear) {
        double decl = 0.0, incl = 0.0, F = 0.0, GV = 0.0;
        double X = 0.0, Y = 0.0, Z = 0.0, H = 0.0;

        int warning = wmm_point(altKm, lat, lon, decimalYear,
                                &decl, &incl, &F, &GV, &X, &Y, &Z, &H);

        if (warning != 0 || std::isnan(decl)) {
            return 0.0;
        }

        return decl;
    }

    double toDecimalYear(int year, int month, int day, int hour, int minute) {
        double dy = 0.0;
        dec_year(year, month, day, hour, minute, &dy);
        return dy;
    }

    bool isInitialized() const { return initialized; }

private:
    bool initialized = false;
};

static void PrintUsage()
{
    std::cout << "Usage: ./autoobservatory <latitude> <longitude> [OPTIONS]" << std::endl;
    std::cout << "Example: ./autoobservatory 45.0 10.0 --date 2026-09-04 --time 14:30" << std::endl;
    std::cout << "Coordinates: decimal degrees (45.5 -10.25) or DEG MIN SEC HEMI,\n"
                 "             e.g. 45 30 15 N 9 12 30 E; S/W are negative." << std::endl;
    std::cout << "  -v, --visible   Show only objects above the horizon" << std::endl;
    std::cout << "  -a, --arduino   Arduino mode: output AZ/ALT for one object only" << std::endl;
    std::cout << "      --dry-run   Validate everything, print the AZ/ALT lines that would\n"
                 "                  be sent, but open no serial device" << std::endl;
    std::cout << "  -m, --magnetic  Output magnetic azimuth (WMM2025)" << std::endl;
    std::cout << "      --rate N    Max slew rate in deg/sec for Arduino tracking (default 2, 0 = off)" << std::endl;
    std::cout << "      --serial DEV  Send AZ/ALT to mount via serial device (e.g. /dev/ttyUSB0)" << std::endl;
    std::cout << "      --baud N    Serial baud rate (default 115200)" << std::endl;
    std::cout << "      --deadzone N  Only send update after moving N deg (serial mode, default 0)" << std::endl;
    std::cout << "      --object NAME  Show/track one object (fuzzy: unique prefix or substring)" << std::endl;
    std::cout << "      --from-planetarium  Use the target selected in ./planetarium --live" << std::endl;
    std::cout << "      --target-file FILE  Selection file to read (default selected_target.txt)" << std::endl;
    std::cout << "  -h, --help      Show this help" << std::endl;
    std::cout << "      --date      Set date (YYYY-MM-DD), defaults to today" << std::endl;
    std::cout << "      --time      Set time (HH:MM or HH:MM:SS), defaults to current time" << std::endl;
    std::cout << std::endl;
    std::cout << "Exit codes: 0 success, 1 runtime error (serial device, missing files,\n"
                 "            unresolvable object), 2 usage error (bad number, unknown\n"
                 "            option, impossible lat/lon)." << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[])
{
    double observerLat = 45.0;
    double observerLon = 10.0;
    bool visibleOnly = false;
    bool arduinoMode = false;
    bool dryRun = false; // print what would be sent instead of opening a serial device
    bool magneticMode = false;
    double slewRate = 2.0; // deg/sec, 0 disables slew limiting
    std::string serialDevice;
    int baudRate = 115200;
    double deadzone = 0.0; // deg; only send when position moved more than this (serial mode)
    std::string targetName;
    std::string dateStr;
    std::string timeStr;

    std::string targetFile = "selected_target.txt";

    if (argc >= 2)
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help")
            {
                PrintUsage();
                return 0;
            }
            else if (arg == "-v" || arg == "--visible")
            {
                visibleOnly = true;
            }
            else if (arg == "-a" || arg == "--arduino")
            {
                arduinoMode = true;
            }
            else if (arg == "--dry-run")
            {
                dryRun = true;
                arduinoMode = true; // dry-run exercises the tracking path
            }
            else if (arg == "-m" || arg == "--magnetic")
            {
                magneticMode = true;
            }
            else if (arg == "--rate" && i + 1 < argc)
            {
                slewRate = ParseDoubleArg(argv[i + 1], "slew rate");
                ++i;
            }
            else if (arg == "--serial" && i + 1 < argc)
            {
                serialDevice = argv[i + 1];
                ++i;
            }
            else if (arg == "--baud" && i + 1 < argc)
            {
                baudRate = ParseIntArg(argv[i + 1], "baud rate");
                ++i;
            }
            else if (arg == "--deadzone" && i + 1 < argc)
            {
                deadzone = ParseDoubleArg(argv[i + 1], "deadzone");
                ++i;
            }
            else if (arg == "--object" && i + 1 < argc)
            {
                targetName = argv[i + 1];
                ++i;
            }
            else if (arg == "--from-planetarium")
            {
                // Pick up the target selected in the live planetarium
                // (n/p to cycle, t to lock; writes selected_target.txt).
                std::ifstream tf(targetFile);
                std::string line;
                if (tf.is_open() && std::getline(tf, line))
                {
                    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
                        line.pop_back();
                    targetName = line;
                }
                if (targetName.empty())
                {
                    std::cerr << "Error: no target in " << targetFile
                              << " - run ./planetarium --live and press t on an object first." << std::endl;
                    return 1;
                }
                std::cout << "Target from planetarium: " << targetName << std::endl;
            }
            else if (arg == "--target-file" && i + 1 < argc)
            {
                targetFile = argv[i + 1];
                ++i;
            }
            else if (arg == "--date" && i + 1 < argc)
            {
                dateStr = argv[i + 1];
                ++i;
            }
            else if (arg == "--time" && i + 1 < argc)
            {
                timeStr = argv[i + 1];
                ++i;
            }
            // Catch unrecognized/incomplete options instead of silently ignoring
            // them (a typo would otherwise start tracking). Negative coordinates
            // ("-33.9") start with '-' too, so only reject a '-' that is not a
            // digit or decimal point.
            else if (arg.size() >= 2 && arg[0] == '-' &&
                     !std::isdigit((unsigned char)arg[1]) && arg[1] != '.')
            {
                std::cerr << "Error: unknown or incomplete option \"" << arg << "\"" << std::endl;
                return 2;
            }
            else
            {
                // Positional coordinate group (decimal or DEG MIN SEC HEMI) -
                // may span 2..8 tokens; gather until the next option.
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
                ParsePositionalCoords(tokens, observerLat, observerLon);
                i = j - 1;
            }
        }
    }
    else
    {
        PrintUsage();
        return 0;
    }

    if (arduinoMode && targetName.empty())
    {
        std::cerr << "Error: --arduino mode requires --object <name> to be specified." << std::endl;
        return 2;
    }

    // Resolve the requested name exactly like ./planetarium --select (shared
    // object_name.h): case-insensitive, unique prefix/substring fuzzy matching,
    // and an error on ambiguity rather than silently aiming at the wrong object.
    if (!targetName.empty())
    {
        std::string canonical;
        std::vector<std::string> candidates;
        if (!ResolveObjectName(LoadCatalogNames(), targetName, canonical, candidates))
        {
            ReportNameFailure(targetName, candidates);
            return 1;
        }
        std::cout << "Target: " << canonical;
        if (!CaseInsensitiveEq(canonical, targetName))
            std::cout << "  (fuzzy match for \"" << targetName << "\")";
        std::cout << std::endl;
        targetName = canonical;
    }

    int serialFd = -1;
    if (dryRun)
    {
        std::cout << "Dry run: no serial device will be opened; lines below are\n"
                     "what would be sent to the mount" << std::endl;
    }
    else if (!serialDevice.empty())
    {
        serialFd = SerialOpen(serialDevice, ParseBaud(baudRate));
        if (serialFd < 0)
        {
            std::cerr << "Error: could not open serial device " << serialDevice
                      << " at " << baudRate << " baud." << std::endl;
            return 1;
        }
        std::cout << "Serial output enabled: " << serialDevice << " @ " << baudRate << " baud" << std::endl;
        if (deadzone > 0.0)
            std::cout << "Deadzone enabled: " << deadzone << "\xC2\xB0" << std::endl;
    }

    LastSent lastSent;

    MagneticDeclination magDecl;
    if (!magDecl.initialize("WMM2025.COF")) {
        std::cerr << "Warning: Failed to initialize WMM2025 magnetic model. Magnetic azimuth unavailable." << std::endl;
    }

    SlewRateLimiter azLimiter(slewRate);
    SlewRateLimiter altLimiter(slewRate);

    std::time_t now = std::time(nullptr);
    std::tm* utcTime = std::gmtime(&now);

    double year = utcTime->tm_year + 1900;
    double month = utcTime->tm_mon + 1;
    double day = utcTime->tm_mday;
    double hour = utcTime->tm_hour;
    double minute = utcTime->tm_min;
    double second = utcTime->tm_sec;
    double millisecond = 0.0;

    if (!dateStr.empty())
        ParseDateArg(dateStr, year, month, day);
    if (!timeStr.empty())
    {
        ParseTimeArg(timeStr, hour, minute, second);
        millisecond = 0.0;
    }

    struct termios origSettings;
    bool terminalReady = SetupTerminal(origSettings);
    // Only warn for an interactive session; when stdin is not a TTY (piped /
    // redirected, e.g. --from-planetarium from a script) failing is expected.
    if (!terminalReady && isatty(STDIN_FILENO))
        std::cerr << "Warning: could not set up terminal for keyboard input." << std::endl;

    char keyBuffer[8] = {0};
    int keyIndex = 0;

    const int REFRESH_INTERVAL_SEC = 30;
    const int TRACK_INTERVAL_MS = 100; // fast tracking (Arduino mode): recompute every 100 ms

    while (true)
    {
        double declination = 0.0;
        if (magneticMode && magDecl.isInitialized()) {
            double decimalYear = magDecl.toDecimalYear(year, month, day, hour, minute);
            declination = magDecl.getDeclination(observerLat, observerLon, 0.0, decimalYear);
        }

        if (arduinoMode)
        {
            DisplayArduinoResults(observerLat, observerLon, targetName,
                                  year, month, day, hour, minute, second, millisecond,
                                  declination, magneticMode,
                                  azLimiter, altLimiter, 0.0, serialFd, deadzone, lastSent, dryRun);
            // One report is the point of a dry run: validate and exit.
            if (dryRun)
            {
                std::cout << "Dry run complete." << std::endl;
                break;
            }
        }
        else
        {
            DisplayResults(observerLat, observerLon, visibleOnly, targetName,
                           year, month, day, hour, minute, second, millisecond,
                           declination, magneticMode);
            std::cout << "Press \"exit\" to quit | Next refresh in " << REFRESH_INTERVAL_SEC << "s" << std::endl;
        }

        bool exited = false;
        auto sleepStart = std::chrono::steady_clock::now();
        auto lastTick = sleepStart;

        if (arduinoMode)
        {
            // Fast tracking: recompute positions every TRACK_INTERVAL_MS
            while (true)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(TRACK_INTERVAL_MS));

                if (CheckExitKey(keyBuffer, keyIndex))
                {
                    exited = true;
                    break;
                }

                auto nowTick = std::chrono::steady_clock::now();
                double dtSec = std::chrono::duration<double>(nowTick - lastTick).count();
                lastTick = nowTick;

                // Refresh wall-clock time on every tick so positions move
                now = std::time(nullptr);
                utcTime = std::gmtime(&now);
                year  = utcTime->tm_year + 1900;
                month = utcTime->tm_mon + 1;
                day   = utcTime->tm_mday;
                hour  = utcTime->tm_hour;
                minute = utcTime->tm_min;
                second = utcTime->tm_sec;
                millisecond = 0.0;

                double declination = 0.0;
                if (magneticMode && magDecl.isInitialized()) {
                    double decimalYear = magDecl.toDecimalYear(year, month, day, hour, minute);
                    declination = magDecl.getDeclination(observerLat, observerLon, 0.0, decimalYear);
                }

                DisplayArduinoResults(observerLat, observerLon, targetName,
                                      year, month, day, hour, minute, second, millisecond,
                                      declination, magneticMode,
                                      azLimiter, altLimiter, dtSec, serialFd, deadzone, lastSent, dryRun);
            }
        }
        else
        {
            while (true)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));

                if (CheckExitKey(keyBuffer, keyIndex))
                {
                    exited = true;
                    break;
                }

                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::steady_clock::now() - sleepStart)
                                   .count();
                if (elapsed >= REFRESH_INTERVAL_SEC) break;
            }
        }

        if (exited)
        {
            std::cout << "Exiting..." << std::endl;
            break;
        }

        now = std::time(nullptr);
        utcTime = std::gmtime(&now);
        year  = utcTime->tm_year + 1900;
        month = utcTime->tm_mon + 1;
        day   = utcTime->tm_mday;
        hour  = utcTime->tm_hour;
        minute = utcTime->tm_min;
        second = utcTime->tm_sec;
        millisecond = 0.0;
    }

    if (terminalReady) RestoreTerminal(origSettings);
    if (serialFd >= 0) SerialClose(serialFd);
    return 0;
}

// Last position handed to the mount, published for the planetarium's tracking
// panel (see ./planetarium --live). One small file, rewritten only when a line
// is actually sent, so the panel can show the real pointing error.
static const char* MOUNT_STATUS_FILE = "mount_status.txt";

static void WriteMountStatus(const std::string& azAltLine, const std::string& target)
{
    std::ofstream f(MOUNT_STATUS_FILE, std::ios::trunc);
    if (!f.is_open()) return;
    f << azAltLine;
    if (!target.empty()) f << " TARGET:" << target;
    f << " T:" << (long)std::time(nullptr) << "\n";
}

void DisplayResults(double observerLat, double observerLon, bool visibleOnly, std::string targetName,
                    double year, double month, double day, double hour, double minute, double second, double millisecond,
                    double magneticDeclination, bool magneticMode)
{
    double jd = JulianDate(year, month, day, hour, minute, second, millisecond);

    std::cout << std::fixed << std::setprecision(4) << std::setfill(' ');
    std::cout << "Observer location: Lat = " << observerLat << "\xC2\xB0, Lon = " << observerLon << "\xC2\xB0" << std::endl;

    std::cout << "Device Time (UTC): " << std::setfill('0')
              << std::setw(4) << (int)year << "-"
              << std::setw(2) << (int)month << "-"
              << std::setw(2) << (int)day << " "
              << std::setw(2) << (int)hour << ":"
              << std::setw(2) << (int)minute << ":"
              << std::setw(2) << (int)second << " UTC" << std::endl;

    std::cout << std::setfill(' ') << std::endl;

    std::vector<CelestialObject> objects = CelestialObjectReader::ReadFromCSV("celestial_objects.csv");
    std::vector<PlanetaryObject> planets = GetPlanetaryObjects(jd);

    PlanetaryObject moon;
    for (const auto& p : planets)
    {
        if (p.name == "Moon")
        {
            moon = p;
            break;
        }
    }

    bool found = false;

    if (targetName.empty())
    {
        std::cout << std::setw(25) << std::left << "Object"
                  << std::setw(13) << std::right << (magneticMode ? "Azimuth (Mag)" : "Azimuth (True)")
                  << std::setw(12) << "Altitude"
                  << std::setw(8) << "Mag"
                  << std::setw(14) << "Status" << std::endl;
        std::cout << std::string(72, '-') << std::endl;
    }

    std::cout << std::fixed << std::setprecision(4);

    if (magneticMode) {
        std::cout << "Magnetic declination: " << magneticDeclination << "\xC2\xB0 (E-positive)" << std::endl;
    }

    for (const auto& obj : objects)
    {
        if (!targetName.empty() && !CaseInsensitiveEq(obj.name, targetName)) continue;
        found = true;

        auto [az, alt] = CalculateHorizontalCoordinates(
            year, month, day, hour, minute, second, millisecond,
            observerLat, observerLon,
            obj.rightAscensionHours, obj.declinationDegrees);

        if (visibleOnly && alt < 0.0) continue;

        if (magneticMode) {
            az = NormalizeDegrees(az - magneticDeclination);
        }

        std::string status = alt < 0 ? "BELOW HORIZON" : "VISIBLE";
        if (status == "VISIBLE" && IsOccultedByMoon(moon.rightAscensionHours, moon.declinationDegrees, obj.rightAscensionHours, obj.declinationDegrees))
        {
            status = "OCCULTED";
        }
        std::cout << std::setw(25) << std::left << obj.name
                  << std::setw(12) << std::right << az << "\xC2\xB0 "
                  << std::setw(11) << alt << "\xC2\xB0 "
                  << std::setw(8) << obj.magnitude
                  << std::setw(14) << status << std::endl;
    }

    std::cout << std::endl;

    for (const auto& planet : planets)
    {
        if (!targetName.empty() && !CaseInsensitiveEq(planet.name, targetName)) continue;
        found = true;

        auto [az, alt] = CalculateHorizontalCoordinates(
            year, month, day, hour, minute, second, millisecond,
            observerLat, observerLon,
            planet.rightAscensionHours, planet.declinationDegrees);

        if (visibleOnly && alt < 0.0) continue;

        if (magneticMode) {
            az = NormalizeDegrees(az - magneticDeclination);
        }

        std::string status = alt < 0 ? "BELOW HORIZON" : "VISIBLE";
        if (planet.name != "Moon" && status == "VISIBLE" && IsOccultedByMoon(moon.rightAscensionHours, moon.declinationDegrees, planet.rightAscensionHours, planet.declinationDegrees))
        {
            status = "OCCULTED";
        }
        std::cout << std::setw(25) << std::left << planet.name
                  << std::setw(12) << std::right << az << "\xC2\xB0 "
                  << std::setw(11) << alt << "\xC2\xB0 "
                  << std::setw(8) << planet.magnitude
                  << std::setw(14) << status << std::endl;
    }

    if (!targetName.empty() && !found)
    {
        std::cout << "Object not found: " << targetName << std::endl;
    }
}

void DisplayArduinoResults(double observerLat, double observerLon, std::string targetName,
                           double year, double month, double day, double hour, double minute, double second, double millisecond,
                           double magneticDeclination, bool magneticMode,
                           SlewRateLimiter& azLimiter, SlewRateLimiter& altLimiter, double dtSec,                            int serialFd, double deadzoneDeg, LastSent& lastSent, bool dryRun)
{
    double jd = JulianDate(year, month, day, hour, minute, second, millisecond);

    std::vector<CelestialObject> objects = CelestialObjectReader::ReadFromCSV("celestial_objects.csv");
    std::vector<PlanetaryObject> planets = GetPlanetaryObjects(jd);

    PlanetaryObject moon;
    for (const auto& p : planets)
    {
        if (p.name == "Moon")
        {
            moon = p;
            break;
        }
    }

    auto findObject = [&](const std::string& name) -> std::pair<double, double>
    {
        for (const auto& obj : objects)
        {
            if (CaseInsensitiveEq(obj.name, name))
            {
                return CalculateHorizontalCoordinates(
                    year, month, day, hour, minute, second, millisecond,
                    observerLat, observerLon,
                    obj.rightAscensionHours, obj.declinationDegrees);
            }
        }
        for (const auto& planet : planets)
        {
            if (CaseInsensitiveEq(planet.name, name))
            {
                return CalculateHorizontalCoordinates(
                    year, month, day, hour, minute, second, millisecond,
                    observerLat, observerLon,
                    planet.rightAscensionHours, planet.declinationDegrees);
            }
        }
        return {0.0, 0.0};
    };

    auto [azTarget, altTarget] = findObject(targetName);

    if (magneticMode) {
        azTarget = NormalizeDegrees(azTarget - magneticDeclination);
    }

    // Smooth the motion so a motorized mount follows without jerky corrections.
    double az = azLimiter.updateWrapping(azTarget, dtSec);
    double alt = altLimiter.update(altTarget, dtSec);

    std::ostringstream line;
    line << std::fixed << std::setprecision(4);
    line << "AZ:" << az << " ALT:" << alt;

    if (serialFd >= 0)
    {
        // Deadzone gating: send only if either axis moved more than the
        // threshold since the last sent update (shortest path for azimuth).
        bool send = true;
        if (deadzoneDeg > 0.0 && lastSent.valid)
        {
            double dAz = NormalizeDegrees(az - lastSent.az);
            if (dAz > 180.0) dAz -= 360.0;
            if (dAz < 0.0) dAz = -dAz;
            double dAlt = std::fabs(alt - lastSent.alt);
            send = (dAz > deadzoneDeg || dAlt > deadzoneDeg);
        }

        if (send)
        {
            SerialWriteLine(serialFd, line.str());
            WriteMountStatus(line.str(), targetName);
            lastSent.az = az;
            lastSent.alt = alt;
            lastSent.valid = true;
        }
    }
    else if (dryRun)
    {
        // Same gating as the real send, but reported instead of transmitted.
        bool send = true;
        if (deadzoneDeg > 0.0 && lastSent.valid)
        {
            double dAz = NormalizeDegrees(az - lastSent.az);
            if (dAz > 180.0) dAz -= 360.0;
            if (dAz < 0.0) dAz = -dAz;
            double dAlt = std::fabs(alt - lastSent.alt);
            send = (dAz > deadzoneDeg || dAlt > deadzoneDeg);
        }
        if (send)
        {
            std::cout << "send: " << line.str() << std::endl;
            lastSent.az = az;
            lastSent.alt = alt;
            lastSent.valid = true;
        }
    }
    else
    {
        std::cout << line.str() << std::endl;
    }
}
