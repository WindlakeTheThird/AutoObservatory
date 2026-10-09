#ifndef FILE_IO_H
#define FILE_IO_H

#include <cstdio>
#include <string>
#include <fstream>
#include <unistd.h>   // getpid()

// Two processes exchange state through plain files in the working directory:
//
//   selected_target.txt   planetarium / GUI  ->  autoobservatory   (a name)
//   mount_status.txt      autoobservatory    ->  planetarium / GUI ("AZ:.. ALT:..")
//
// mount_status.txt is rewritten on every serial send, which is up to 10 Hz while
// the GUI samples it at 1 Hz and the planetarium every refresh. A plain
// truncating write (`std::ofstream f(path, std::ios::trunc)`) opens that window:
// a reader can land between the truncate and the write and see an empty or
// half-written file, which it then reports as "no position recorded" -- a stale
// mount looking like a dead one.
//
// Write to a temporary file in the SAME directory and rename() into place.
// rename(2) is atomic within a filesystem, so a concurrent reader observes
// either the whole previous file or the whole new one, never a partial write.
// The temp name includes the pid so two writers cannot collide, and lives beside
// the target so rename cannot cross a filesystem boundary (which would make it
// non-atomic). Leftover temp files are removed on the next write and by
// `make clean`.

inline bool WriteFileAtomic(const std::string& path, const std::string& content)
{
    const std::string tmp = path + ".tmp." + std::to_string(static_cast<long>(getpid()));

    {
        std::ofstream f(tmp, std::ios::trunc | std::ios::binary);
        if (!f.is_open()) return false;
        f << content;
        f.flush();
        if (!f.good()) { f.close(); std::remove(tmp.c_str()); return false; }
    }

    if (std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

#endif