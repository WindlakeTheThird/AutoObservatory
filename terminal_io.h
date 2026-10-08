#ifndef TERMINAL_IO_H
#define TERMINAL_IO_H

#include <unistd.h>
#include <termios.h>

inline bool SetupTerminal(struct termios& originalSettings)
{
    if (tcgetattr(STDIN_FILENO, &originalSettings) < 0) return false;

    struct termios raw = originalSettings;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) return false;
    return true;
}

inline void RestoreTerminal(const struct termios& originalSettings)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalSettings);
}

inline bool CheckExitKey(char* keyBuffer, int& keyIndex)
{
    char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n <= 0) return false;

    if (ch == 27)
    {
        return true;
    }

    if (keyIndex < 7) keyBuffer[keyIndex++] = ch;

    if (keyIndex >= 4 &&
        keyBuffer[keyIndex - 4] == 'e' &&
        keyBuffer[keyIndex - 3] == 'x' &&
        keyBuffer[keyIndex - 2] == 'i' &&
        keyBuffer[keyIndex - 1] == 't')
    {
        return true;
    }
    return false;
}

#endif
