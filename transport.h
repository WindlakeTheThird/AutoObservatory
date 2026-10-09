#ifndef TRANSPORT_H
#define TRANSPORT_H

// Transport abstraction for the link to the mount.
//
// The tracking runtime used to talk to the mount through a bare `int` file
// descriptor, which made the link permanently a USB serial cable. It is now an
// object, so the same code can drive the mount over a socket -- see
// ObsMCP/Wireless Mount Link.md for the phased plan.
//
// Deliberately narrow: the wire protocol (AZ:/ALT:/SEQ:/CRC:) lives in
// mount_control.h and is identical on every transport. A transport only moves
// whole lines. That separation is what keeps an ESP32 byte-pipe bridge from
// ever becoming a second implementation of the astronomy.

#include <string>
#include <memory>
#include <cstdint>

// --- wire format ------------------------------------------------------------
//
// One command per line, terminated by '\n'. Framing is newline-only: there is
// no escaping, so no field may contain a newline (object names never do).
//
//   AZ:<deg> ALT:<deg> SEQ:<n> CRC:<hhhh>\n
//
// SEQ counts from 1 and increments per command. CRC is a CRC-16/CCITT-FALSE
// over every byte of the line preceding the " CRC:" token, uppercase hex.
// Together they turn a silently-truncated line on a lossy radio link into a
// detectable one. The sketch rejects a bad CRC and counts a sequence gap.
//
// Pure helpers, no I/O: the host formats lines, the sketch parses them, and
// the cross-language check compares a formatted line against a fixed vector.

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final xor).
// The standard check value for "123456789" is 0x29B1; tools/wirecheck.cpp
// asserts it, so a refactor cannot silently change the algorithm and still
// agree with the sketch.
inline unsigned Crc16CcittFalse(const std::string& data)
{
    unsigned crc = 0xFFFF;
    for (char ch : data)
    {
        crc ^= static_cast<unsigned char>(ch) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
    }
    return crc & 0xFFFF;
}

// Append " SEQ:<n> CRC:<hhhh>" to a line that ends at 'line.size()'.
//
// The CRC covers every byte before the " CRC:" token, which includes the
// sequence number -- so a corrupted SEQ is caught as well as corrupted
// coordinates. The sketch's checkWireIntegrity() measures the same span; the
// two MUST stay in step or every command is rejected.
inline std::string AppendWireCheck(const std::string& line, long seq)
{
    std::string withSeq = line + " SEQ:" + std::to_string(seq);
    char buf[16];
    std::snprintf(buf, sizeof(buf), " CRC:%04X", Crc16CcittFalse(withSeq));
    return withSeq + buf;
}

// Format a target command with the integrity fields appended. Only the AZ/ALT
// target line carries SEQ/CRC: it is the only one that can move the mount, so
// it is the only one where a corrupted byte must be caught. CAL and DECL are
// single-use setup commands sent over a link the operator is watching.
inline std::string FormatTargetLine(double az, double alt, long seq)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "AZ:%.4f ALT:%.4f", az, alt);
    return AppendWireCheck(buf, seq);
}

// --- transports -------------------------------------------------------------

// A byte pipe to the mount. write() takes a line without its terminator (the
// transport appends '\n'), read() returns one line with the terminator stripped.
class Transport
{
public:
    virtual ~Transport() = default;

    virtual bool open(const std::string& spec, int baud, std::string* err) = 0;
    virtual bool write(const std::string& line) = 0;
    virtual bool read(std::string& out, int timeoutMs) = 0;
    virtual bool connected() const = 0;
    virtual void close() = 0;

    // Human-readable description for the startup banner.
    virtual std::string describe() const = 0;
};

// No device at all: commands go to stdout for a human to read. This is the
// default when neither --mount nor --serial is given, and it is what the
// regression suite exercises, so it must never touch the filesystem.
class StdoutTransport : public Transport
{
public:
    bool open(const std::string& spec, int baud, std::string* err) override
    {
        (void)spec; (void)baud; (void)err;
        return true;
    }
    bool write(const std::string& line) override
    {
        std::fputs(line.c_str(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
        return true;
    }
    bool read(std::string& out, int timeoutMs) override
    {
        (void)out; (void)timeoutMs;
        return false; // nothing to read back
    }
    bool connected() const override { return true; }
    void close() override {}
    std::string describe() const override { return "stdout"; }
};

// USB serial, byte-for-byte today's behaviour. O_RDWR: the host must be able to
// read the sketch's ERR:/CAL: telemetry, which it previously could not (the
// port was O_WRONLY, so calibrate's read-back path never worked).
class SerialTransport : public Transport
{
public:
    ~SerialTransport() override { close(); }

    bool open(const std::string& spec, int baud, std::string* err) override;
    bool write(const std::string& line) override;
    bool read(std::string& out, int timeoutMs) override;
    bool connected() const override { return fd_ >= 0; }
    void close() override;
    std::string describe() const override;

private:
    int fd_ = -1;
    std::string device_;
    int baud_ = 0;
    std::string rxBuf_;
};

// Plain TCP, for a wireless bridge speaking the same line protocol. Blocking
// with a poll() timeout; no TLS (see ObsMCP/Wireless Mount Link.md Phase 4).
class TcpTransport : public Transport
{
public:
    ~TcpTransport() override { close(); }

    bool open(const std::string& spec, int baud, std::string* err) override;
    bool write(const std::string& line) override;
    bool read(std::string& out, int timeoutMs) override;
    bool connected() const override { return fd_ >= 0; }
    void close() override;
    std::string describe() const override;

private:
    int fd_ = -1;
    std::string host_;
    int port_ = 0;
    std::string rxBuf_;
};

// --- spec parsing -----------------------------------------------------------

// "tcp://host:port" | "serial:/dev/ttyUSB0" | "/dev/ttyUSB0" | "stdin" |
// "stdout". Returns a transport, or nullptr with *err set on a bad spec.
// `stdin` maps to StdoutTransport: the point of the scheme is that a caller
// can be pointed at a loopback pipe in tests without special-casing.
inline std::unique_ptr<Transport> MakeTransport(const std::string& spec,
                                               int baud, std::string* err)
{
    auto fail = [&](const std::string& msg) -> std::unique_ptr<Transport>
    {
        if (err) *err = msg;
        return nullptr;
    };

    std::string s = spec;
    if (s == "stdin" || s == "stdout") return std::make_unique<StdoutTransport>();

    if (s.rfind("tcp://", 0) == 0)
    {
        std::string rest = s.substr(6);
        size_t colon = rest.rfind(':');
        if (colon == std::string::npos || colon + 1 >= rest.size())
            return fail("tcp spec needs host:port, got \"" + spec + "\"");
        std::string host = rest.substr(0, colon);
        std::string portStr = rest.substr(colon + 1);
        int port = 0;
        for (char ch : portStr)
        {
            if (ch < '0' || ch > '9')
                return fail("tcp port must be numeric in \"" + spec + "\"");
            port = port * 10 + (ch - '0');
            if (port > 65535) return fail("tcp port out of range in \"" + spec + "\"");
        }
        if (host.empty() || port == 0)
            return fail("tcp spec needs host:port, got \"" + spec + "\"");
        auto t = std::make_unique<TcpTransport>();
        if (!t->open(s, baud, err)) return nullptr;
        return t;
    }

    if (s.rfind("serial://", 0) == 0) s = s.substr(9);
    if (s.rfind("bluetooth://", 0) == 0)
        return fail("bluetooth:// needs a paired device bound to a local port "
                    "(e.g. bluetooth:/dev/rfcomm0); bind it first, then use that path");
    if (s.rfind("udp://", 0) == 0)
        return fail("udp is not supported: the mount protocol is a reliable ordered line stream");

    if (s.empty()) return fail("empty mount spec");
    auto t = std::make_unique<SerialTransport>();
    if (!t->open(s, baud, err)) return nullptr;
    return t;
}

// True when the spec names no device (the plain-stdout default).
inline bool IsDeviceSpec(const std::string& spec)
{
    return !spec.empty() && spec != "stdin" && spec != "stdout";
}

#endif