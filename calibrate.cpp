#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>
#include <ctime>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include "mount_control.h"
#include "astro_math.h"
#include "wmm_mag.h"
#include "cli_parse.h"
#include <vector>

// ponytail: same wrapper as main.cpp's MagneticDeclination — extract to a
// shared header if a third consumer appears.
//
// NOTE the init test: wmm_init returns NON-ZERO ON SUCCESS (1 for a loaded
// model, 0 when the COF cannot be opened). Testing `== 0` here made init
// always fail, so declination silently stayed 0.0 and DECL:0.0000 was sent and
// stored in the sketch's EEPROM -- a ~3.8 deg azimuth error at 45N 10E, roughly
// 25x the firmware deadband. main.cpp had this right; the two had drifted.
struct MagDecl {
    bool init(const char* cof = "WMM2025.COF") { return wmm_init(cof) != 0; }
    double declination(double lat, double lon, double year) {
        double decl, incl, F, GV, X, Y, Z, H;
        if (wmm_point(0.0, lat, lon, year, &decl, &incl, &F, &GV, &X, &Y, &Z, &H) != 0)
            return 0.0;
        return decl;
    }
};

// calibrate.cpp — one-shot mount calibration for the LSM303AGR mount follower.
//
// Run BEFORE autoobservatory tracking (the sketch refuses targets until
// calibrated). Sequence:
//   1. Tells the mount to slew to magnetic north (AZ:0) at ALT 30.
//   2. Waits for the mount to report "CAL:az off=N alt off=N" (sketch computes
//      offsets so its sensor frame matches the commanded magnetic frame).
//   3. Queries WMM true north for the observer and sends "DECL:<deg>" so the
//      sketch can convert its magnetic heading to true azimuth.
//   4. Verifies: sends AZ:0 (true north) and checks the reported error < 2 deg.
//
// Usage: calibrate <lat> <lon> [serial-device] [baud]

static bool ReadLine(int fd, std::string& out, int timeoutMs)
{
    out.clear();
    auto start = std::chrono::steady_clock::now();
    char c;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count() < timeoutMs)
    {
        ssize_t n = read(fd, &c, 1);
        if (n == 1)
        {
            if (c == '\n' || c == '\r')
            {
                if (!out.empty()) return true;
            }
            else out += c;
        }
        else std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

static int runMain(int argc, char* argv[])
{
    bool dryRun = false;
    const char* helpMsg =
        "Usage: calibrate <lat> <lon> [serial-device] [baud] [--dry-run]\n"
        "  Coordinates: decimal degrees or DEG MIN SEC HEMI (45 30 15 N 9 12 30 E);\n"
        "               minutes need a hemisphere letter, so '45 30' = 45N 30E.\n"
        "  --dry-run  print the serial lines that would be sent (incl. the WMM\n"
        "             DECL value) without opening any device\n"
        "Exit codes: 0 success, 1 runtime error (no CAL report, no convergence,\n"
        "            serial device), 2 usage error (bad lat/lon/baud, no args)\n";
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { std::fputs(helpMsg, stderr); return 0; }
        if (a == "--dry-run") dryRun = true;
    }
    if (argc < 3)
    {
        std::fputs(helpMsg, stderr);
        return 2;
    }
    // Leading positional tokens form the coordinate group (decimal or DMS);
    // the first token that is neither numeric nor a hemisphere letter starts
    // the positional [serial-device] [baud].
    std::vector<std::string> coordTokens;
    std::vector<std::string> rest;
    for (int i = 1; i < argc; ++i)
    {
        std::string s = argv[i];
        bool isCoord = !s.empty() && (std::isdigit((unsigned char)s[0]) || s[0] == '.' ||
                       (s[0] == '-' && s.size() > 1 &&
                        (std::isdigit((unsigned char)s[1]) || s[1] == '.')) ||
                       (s.size() == 1 && IsHemisphere(s)));
        if (rest.empty() && isCoord)
            coordTokens.push_back(s);
        else if (s != "--dry-run") // flags were handled above; never a device name
            rest.push_back(s);
    }
    if (coordTokens.size() < 2)
    {
        if (!rest.empty() && LooksNumeric(rest[0]))
            std::cerr << "Error: invalid coordinate \"" << rest[0] << "\"\n";
        std::fputs(helpMsg, stderr);
        return 2;
    }
    double lat = 0, lon = 0;
    ParsePositionalCoords(coordTokens, lat, lon);
    std::string dev = rest.empty() ? "/dev/ttyUSB0" : rest[0];
    int baud = rest.size() > 1 ? ParseIntArg(rest[1].c_str(), "baud rate") : 115200;

// 3's WMM declination is computed up front so --dry-run can print it.
      //
      // Decimal year now comes from the system clock. It was hardcoded to
      // 2026.72, which goes stale: declination drifts roughly 0.2 deg/yr, the
      // same order as the azimuth error calibration exists to remove.
      std::time_t now = std::time(nullptr);
      std::tm utc{};
      gmtime_r(&now, &utc);
      double year = (utc.tm_year + 1900) + utc.tm_yday / 365.25;

      MagDecl magDecl;
      // A wrong DECL is worse than no calibration: it is stored in the sketch's
      // EEPROM and silently biases every later azimuth. Fail loudly instead of
      // defaulting to 0.0 and reporting success.
      if (!magDecl.init())
      {
          std::fprintf(stderr,
              "Cannot load the WMM model (WMM2025.COF). Run from the project root,\n"
              " or pass an explicit model path. Refusing to calibrate with an\n"
              " unknown declination - a wrong DECL is stored in EEPROM.\n");
          return 1;
      }
      double decl = magDecl.declination(lat, lon, year);
      if (decl == 0.0)
      {
          std::fprintf(stderr,
              "WMM returned no declination for %.3f, %.3f. Refusing to continue:\n"
              " a zero here would be stored in EEPROM and bias every azimuth.\n", lat, lon);
          return 1;
      }

    if (dryRun)
    {
        std::printf("Dry run for %s @ %d baud (no device opened):\n", dev.c_str(), baud);
        std::printf("  send: AZ:0 ALT:30        (slew to magnetic north)\n");
        std::printf("  send: CAL                (sketch self-calibration, waits for CAL: report)\n");
        std::printf("  send: DECL:%.4f     (WMM declination at %.2f, %.2f)\n", decl, lat, lon);
        std::printf("  send: AZ:0 ALT:30        (verify: slew to TRUE north, expect ERR: < 2 deg)\n");
        return 0;
    }

    int fd = SerialOpen(dev, ParseBaud(baud));
    if (fd < 0) { std::fprintf(stderr, "Cannot open %s\n", dev.c_str()); return 1; }

    // 1. Command magnetic north. The mount slews until its tilt-compensated
    //    heading reads 0 — that defines the magnetic frame, wherever it points.
    std::printf("Step 1: slewing to magnetic north (AZ:0 ALT:30)...\n");
    SerialWriteLine(fd, "AZ:0 ALT:30");
    std::this_thread::sleep_for(std::chrono::seconds(2)); // let slew start

    // 1b. Put the sketch into calibration mode (zeroes offsets, slews to
    //     magnetic north, computes + stores offsets, replies CAL:...).
    std::printf("Step 1b: starting sketch self-calibration...\n");
    SerialWriteLine(fd, "CAL");

    // 2. Wait for calibration report from the sketch.
    std::string line;
    double azOff = 0.0, altOff = 0.0;
    bool gotCal = false;
    std::printf("Step 2: waiting for mount to settle and report offsets...\n");
    for (int i = 0; i < 120 && !gotCal; ++i) // up to ~2 min
    {
        if (ReadLine(fd, line, 1000) && std::sscanf(line.c_str(), "CAL:%*f %*f azoff=%lf altoff=%lf", &azOff, &altOff) == 2)
            gotCal = true;
    }
    if (!gotCal)
    {
        std::fprintf(stderr, "Timed out waiting for CAL report. Is the sketch in calibration mode?\n");
        SerialClose(fd);
        return 1;
    }
    std::printf("  mount reports az offset %+.2f, alt offset %+.2f — stored on the Arduino (EEPROM).\n", azOff, altOff);

    // 3. Magnetic -> true conversion: WMM declination at this site/date.
    //    The sketch adds this to its (magnetic) heading so subsequent AZ:
    //    commands are interpreted as TRUE azimuth.
    char buf[48];
    std::snprintf(buf, sizeof(buf), "DECL:%.4f", decl);
    SerialWriteLine(fd, buf);
    std::printf("Step 3: sent DECL:%.2f (WMM magnetic declination at %.2f, %.2f)\n", decl, lat, lon);

    // 4. Verify: command true north and read back the steady-state error.
    std::printf("Step 4: verifying — slewing to true north (AZ:0)...\n");
    SerialWriteLine(fd, "AZ:0 ALT:30");
    // The sketch now has magDeclination applied, so AZ:0 = TRUE north. Its
    // ERR: lines report steady-state error; wait for it to settle.
    double errAz = 999, errAlt = 999;
    for (int i = 0; i < 60; ++i)
    {
        // Re-send the target every second. The sketch's safety watchdog stops
        // the motors after CMD_TIMEOUT_MS without a fresh command, so a host
        // that commands once and then only listens would stall the mount
        // mid-travel. Keeping the command alive is what the watchdog expects.
        SerialWriteLine(fd, "AZ:0 ALT:30");
        if (ReadLine(fd, line, 1000))
        {
            // A STALE line means the sketch gave up driving; never treat that
            // as a successful calibration, whatever the error fields say.
            if (line.find("STALE") != std::string::npos)
            {
                std::fprintf(stderr, "Mount reports STALE (command link lost) - not calibrated.\n");
                SerialClose(fd);
                return 1;
            }
            if (std::sscanf(line.c_str(), "ERR:%lf %lf", &errAz, &errAlt) == 2 &&
                std::fabs(errAz) < 2.0 && std::fabs(errAlt) < 2.0)
            {
                std::printf("  OK: az err %.2f, alt err %.2f — mount is calibrated to TRUE north.\n", errAz, errAlt);
                SerialClose(fd);
                return 0;
            }
        }
    }
    std::fprintf(stderr, "Verification did not converge (last err az %.2f alt %.2f).\n", errAz, errAlt);
    SerialClose(fd);
    return 1;
}

// Any exception escaping the program (malformed CSV row, missing data file)
// must surface as exit 1 with a message, per the documented exit-code
// convention: 0 success, 1 runtime error, 2 usage error. Without this the
// process terminated via std::terminate with SIGABRT (exit 134).
int main(int argc, char* argv[])
{
    try
    {
        return runMain(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
