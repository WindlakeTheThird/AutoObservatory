#ifndef MOUNT_CONTROL_H
#define MOUNT_CONTROL_H

#include <string>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include "astro_math.h"

// Serial port output for Arduino/mount control
inline speed_t ParseBaud(int baud)
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

inline int SerialOpen(const std::string& device, speed_t baud)
{
    int fd = open(device.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0)
    {
        close(fd);
        return -1;
    }

    // 8N1, no flow control, raw output
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= (CREAD | CLOCAL);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        close(fd);
        return -1;
    }

    return fd;
}

inline void SerialWriteLine(int fd, const std::string& line)
{
    if (fd < 0) return;
    std::string out = line + "\n";
    ssize_t n = write(fd, out.c_str(), out.size());
    (void)n; // best-effort: mount may not be listening
}

inline void SerialClose(int fd)
{
    if (fd >= 0) close(fd);
}

// Tracks the last position actually sent to the mount, for deadzone gating.
struct LastSent
{
    bool valid = false;
    double az = 0.0;
    double alt = 0.0;
};

// Smooths motion toward a target at a bounded rate (deg/sec) so a motorized
// mount follows without jerky corrections. dtSec is the time since the last
// update in seconds; maxRateDegPerSec <= 0 disables limiting.
class SlewRateLimiter
{
public:
    explicit SlewRateLimiter(double maxRateDegPerSec = 0.0)
        : maxRate(maxRateDegPerSec), current(0.0), initialized(false) {}

    // Normal (non-wrapping) axis, e.g. altitude [-90, 90].
    double update(double target, double dtSec)
    {
        if (!initialized) { current = target; initialized = true; return current; }
        if (maxRate > 0.0 && dtSec > 0.0)
        {
            double maxStep = maxRate * dtSec;
            double diff = target - current;
            if (diff > maxStep) diff = maxStep;
            if (diff < -maxStep) diff = -maxStep;
            current += diff;
        }
        else
        {
            current = target;
        }
        return current;
    }

    // Wrapping axis in degrees, e.g. azimuth [0, 360). Takes the shortest
    // angular path across the 0/360 boundary.
    double updateWrapping(double target, double dtSec)
    {
        if (!initialized) { current = target; initialized = true; return NormalizeDegrees(current); }
        if (maxRate > 0.0 && dtSec > 0.0)
        {
            double maxStep = maxRate * dtSec;
            double diff = NormalizeDegrees(target - current);
            if (diff > 180.0) diff -= 360.0; // shortest path, [-180, 180]
            if (diff > maxStep) diff = maxStep;
            if (diff < -maxStep) diff = -maxStep;
            current = NormalizeDegrees(current + diff);
        }
        else
        {
            current = target;
        }
        return NormalizeDegrees(current);
    }

private:
    double maxRate;
    double current;
    bool initialized;
};

#endif
