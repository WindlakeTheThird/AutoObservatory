#ifndef TRANSPORT_POSIX_H
#define TRANSPORT_POSIX_H

// POSIX implementation of the transports declared in transport.h. Kept separate
// so the interface can be reviewed (and unit-tested for its pure helpers)
// without the socket/termios includes.

#include "transport.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <cerrno>
#include <csignal>

inline speed_t BaudToSpeed(int baud)
{
    switch (baud)
    {
        case 9600:    return B9600;
        case 19200:   return B19200;
        case 38400:   return B38400;
        case 57600:   return B57600;
        case 115200:  return B115200;
        case 230400:  return B230400;
        default:      return B115200;
    }
}

// Pull one complete line out of `rxBuf` (terminator stripped). False if the
// buffer holds no complete line yet. Buffer is capped so a babbling peer with
// no newline cannot grow it without bound.
template <typename Buf>
inline bool TakeLine(Buf& rxBuf, std::string& out)
{
    const size_t kMaxRx = 8192;
    if (rxBuf.size() > kMaxRx) rxBuf.clear(); // runaway with no terminator
    size_t nl = rxBuf.find('\n');
    if (nl == std::string::npos) return false;
    out = rxBuf.substr(0, nl);
    if (!out.empty() && out.back() == '\r') out.pop_back();
    rxBuf.erase(0, nl + 1);
    return true;
}

// True when a newline-terminated write completed in full. A short write means
// the link is in trouble and the caller must treat it as a failure rather than
// silently continue: a partially-delivered command line would otherwise reach
// the sketch as a truncated, plausible-looking target.
inline bool WriteAll(int fd, const std::string& out)
{
    size_t off = 0;
    while (off < out.size())
    {
        ssize_t n = ::write(fd, out.data() + off, out.size() - off);
        if (n > 0) { off += static_cast<size_t>(n); continue; }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        return false;
    }
    return true;
}

// --- SerialTransport --------------------------------------------------------

inline bool SerialTransport::open(const std::string& spec, int baud, std::string* err)
{
    // O_RDWR, not O_WRONLY: the sketch emits ERR:/CAL:/STALE: telemetry and the
    // host must be able to read it (calibrate's read-back path previously
    // could not, because the port was write-only and read() returned EBADF).
    int fd = ::open(spec.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        if (err) *err = std::string("could not open serial device ") + spec
                       + ": " + std::strerror(errno);
        return false;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0)
    {
        if (err) *err = std::string("could not read termios for ") + spec + ": "
                       + std::strerror(errno);
        ::close(fd);
        return false;
    }

    // 8N1, no flow control, raw in both directions.
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= (CREAD | CLOCAL);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    speed_t sp = BaudToSpeed(baud);
    cfsetispeed(&tty, sp);
    cfsetospeed(&tty, sp);

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        if (err) *err = std::string("could not set termios for ") + spec + ": "
                       + std::strerror(errno);
        ::close(fd);
        return false;
    }

    fd_ = fd;
    device_ = spec;
    baud_ = baud;
    rxBuf_.clear();
    return true;
}

inline bool SerialTransport::write(const std::string& line)
{
    if (fd_ < 0) return false;
    return WriteAll(fd_, line + "\n");
}

inline bool SerialTransport::read(std::string& out, int timeoutMs)
{
    if (fd_ < 0) return false;
    struct pollfd p{ fd_, POLLIN, 0 };
    int pr = poll(&p, 1, timeoutMs);
    if (pr <= 0) return false;
    char buf[512];
    ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n <= 0) return false;
    rxBuf_.append(buf, static_cast<size_t>(n));
    return TakeLine(rxBuf_, out);
}

inline void SerialTransport::close()
{
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    rxBuf_.clear();
}

inline std::string SerialTransport::describe() const
{
    return device_ + " @ " + std::to_string(baud_) + " baud";
}

// --- TcpTransport -----------------------------------------------------------

inline bool TcpTransport::open(const std::string& spec, int baud, std::string* err)
{
    // Without this, writing to a bridge that has just dropped the connection
    // raises SIGPIPE and kills the process outright. Over a radio link that
    // happens routinely -- and killing the host mid-slew means the sketch's
    // watchdog fires 5 s later and the mount stops. Ignoring it turns the drop
    // into an EPIPE return value, which write() already reports as a failure.
    ::signal(SIGPIPE, SIG_IGN);

    std::string rest = spec.substr(6); // strip "tcp://"
    size_t colon = rest.rfind(':');
    host_ = rest.substr(0, colon);
    port_ = std::stoi(rest.substr(colon + 1));
    (void)baud; // meaningless for a socket; accepted so the signature is uniform

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    const std::string portStr = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0 || !res)
    {
        if (err) *err = "could not resolve " + host_;
        return false;
    }

    int fd = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0)
    {
        if (err) *err = "could not connect to " + host_ + ":" + portStr;
        return false;
    }
    fd_ = fd;
    rxBuf_.clear();
    return true;
}

inline bool TcpTransport::write(const std::string& line)
{
    if (fd_ < 0) return false;
    return WriteAll(fd_, line + "\n");
}

inline bool TcpTransport::read(std::string& out, int timeoutMs)
{
    if (fd_ < 0) return false;
    struct pollfd p{ fd_, POLLIN, 0 };
    int pr = poll(&p, 1, timeoutMs);
    if (pr <= 0) return false;
    char buf[512];
    ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n <= 0) return false;
    rxBuf_.append(buf, static_cast<size_t>(n));
    return TakeLine(rxBuf_, out);
}

inline void TcpTransport::close()
{
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    rxBuf_.clear();
}

inline std::string TcpTransport::describe() const
{
    return "tcp " + host_ + ":" + std::to_string(port_);
}

#endif