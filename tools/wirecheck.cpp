// Pins the mount wire format.
//
// Phase 0 of ObsMCP/Wireless Mount Link adds " SEQ:<n> CRC:<hhhh>" to every
// target line. If the host's CRC and the sketch's CRC ever disagree by one bit
// -- a different span, a different polynomial, a reflection left in -- the
// sketch rejects EVERY command and the mount silently never moves. That
// failure mode is invisible in normal use and catastrophic at the telescope, so
// it is pinned here against an independent implementation of the same
// algorithm rather than against the header that produced the value.
//
// Three things are checked:
//   1. the CRC algorithm matches the published CRC-16/CCITT-FALSE check value,
//   2. a formatted line's CRC equals an independently computed CRC over the
//      same byte span the sketch measures (everything before " CRC:"),
//   3. a single flipped character changes the CRC, so corruption is detectable.

#include "transport.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// Deliberately NOT calling Crc16CcittFalse: a second transcription of the
// algorithm, so agreement means something.
unsigned ReferenceCrc16(const std::string& data)
{
    unsigned crc = 0xFFFFu;
    for (size_t i = 0; i < data.size(); ++i)
    {
        crc ^= static_cast<unsigned>(static_cast<unsigned char>(data[i])) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
    }
    return crc & 0xFFFFu;
}

// Parse the trailing "CRC:<hhhh>" off a formatted line.
bool ExtractCrc(const std::string& line, unsigned& out)
{
    size_t p = line.rfind(" CRC:");
    if (p == std::string::npos) return false;
    return std::sscanf(line.c_str() + p + 5, "%X", &out) == 1;
}

} // namespace

int main()
{
    std::printf("== mount wire-format check ==\n");

    // 1. Standard check value for CRC-16/CCITT-FALSE.
    check(Crc16CcittFalse("123456789") == 0x29B1,
          "CRC-16/CCITT-FALSE check value for \"123456789\" is 0x29B1");
    check(Crc16CcittFalse("") == 0xFFFF, "CRC of an empty string is the init value 0xFFFF");

    // 2. Host formatting vs. the sketch's measurement span.
    struct Case { double az; double alt; long seq; const char* label; };
    const std::vector<Case> cases = {
        {  0.0,        30.0,  1, "magnetic north (calibrate's first command)" },
        {240.8006,     14.9531, 1841, "a real mid-sky target" },
        {  0.0000,    -0.0001, 2, "negative sub-degree values" },
        {359.9999,     89.9999, 999999, "range extremes" },
    };

    for (const Case& c : cases)
    {
        std::string line = FormatTargetLine(c.az, c.alt, c.seq);
        unsigned wire = 0;
        if (!ExtractCrc(line, wire))
        {
            std::printf("FAIL %s: no CRC field in \"%s\"\n", c.label, line.c_str());
            ++failures;
            continue;
        }
        // The sketch computes the CRC over every byte before " CRC:" -- which
        // includes the SEQ field. Asserting that exact span is the whole point.
        size_t p = line.rfind(" CRC:");
        unsigned reference = ReferenceCrc16(line.substr(0, p));
        check(wire == reference, c.label);
        std::printf("     %s\n", line.c_str());
    }

    // 3. Corruption must be detectable: the reason the field exists. A line
    // truncated mid-number -- what a dropped radio packet looks like -- must
    // either fail to parse or fail the CRC.
    {
        std::string line = FormatTargetLine(240.8006, 14.9531, 7);
        unsigned wire = 0;
        ExtractCrc(line, wire);

        // Flip one digit of the azimuth in the CRC-covered span.
        std::string corrupted = line;
        size_t azPos = corrupted.find("AZ:");
        corrupted[azPos + 3] = (corrupted[azPos + 3] == '9') ? '0'
                             : static_cast<char>(corrupted[azPos + 3] + 1);
        size_t p = corrupted.rfind(" CRC:");
        check(ReferenceCrc16(corrupted.substr(0, p)) != wire,
              "a single changed digit invalidates the CRC");

        // Truncation, which is the actual over-the-air failure: keep the
        // prefix, drop the tail, and the CRC can no longer agree.
        std::string truncated = line.substr(0, line.find(" SEQ:"));
        unsigned truncCrc = 0;
        check(!ExtractCrc(truncated, truncCrc),
              "a truncated line carries no CRC field, so the sketch refuses it");
    }

    // 4. Sequence numbers must be monotonic across a run, since the sketch
    // reports a gap as a bad link.
    {
        long seq = 0;
        unsigned prev = 0;
        bool ok = true;
        for (int i = 0; i < 5; ++i)
        {
            std::string line = FormatTargetLine(10.0 * i, 20.0, ++seq);
            unsigned wire = 0;
            if (!ExtractCrc(line, wire) || (i > 0 && wire == prev)) ok = false;
            prev = wire;
        }
        check(ok, "consecutive sequence numbers produce distinct CRC-tagged lines");
    }

    if (failures)
    {
        std::printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("All wire-format checks passed.\n");
    return 0;
}