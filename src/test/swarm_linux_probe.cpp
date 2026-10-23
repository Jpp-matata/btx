// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// Helper for the Linux swarm confinement tests. It is linked statically
// because the filesystem jail has no dynamic loader below its root. The tests
// copy it into a root and run it through SwarmRunConfined or
// SwarmRunFilesystemJail with no arguments; it works in its own directory
// (/ inside the jail). A marker file there selects a mode:
// spread.mode writes up to 64 files of 1 MiB, 20 ms apart; burst.mode writes
// three files of 1 MiB at once and exits. Each mode writes <mode>.started
// before and <mode>.done after its work. Without a mode it records its
// RLIMIT_FSIZE in probe.out.

#include <fcntl.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace {

char g_dir[4096];
char g_block[1 << 20];

const char* At(const char* name)
{
    static char path[sizeof(g_dir) + 64];
    std::snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    return path;
}

bool Exists(const char* name)
{
    return access(At(name), F_OK) == 0;
}

void Mark(const char* name)
{
    const int fd = open(At(name), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) close(fd);
}

bool WriteBlock(const char* name)
{
    const int fd = open(At(name), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    const bool full = write(fd, g_block, sizeof(g_block)) == static_cast<ssize_t>(sizeof(g_block));
    return close(fd) == 0 && full;
}

} // namespace

int main(int argc, char** argv)
{
    const char* self = argc > 0 && argv[0] != nullptr ? argv[0] : "";
    const char* slash = std::strrchr(self, '/');
    if (slash == nullptr || static_cast<size_t>(slash - self) >= sizeof(g_dir)) return 2;
    std::memcpy(g_dir, self, static_cast<size_t>(slash - self));
    std::memset(g_block, 'x', sizeof(g_block));
    char name[32];
    if (Exists("spread.mode")) {
        Mark("spread.started");
        const timespec pause{0, 20 * 1000 * 1000};
        for (int i = 0; i < 64; ++i) {
            std::snprintf(name, sizeof(name), "spread%d.bin", i);
            if (!WriteBlock(name)) return 3;
            nanosleep(&pause, nullptr);
        }
        Mark("spread.done");
        return 0;
    }
    if (Exists("burst.mode")) {
        Mark("burst.started");
        for (int i = 0; i < 3; ++i) {
            std::snprintf(name, sizeof(name), "burst%d.bin", i);
            if (!WriteBlock(name)) return 4;
        }
        Mark("burst.done");
        return 0;
    }
    rlimit limit{};
    if (getrlimit(RLIMIT_FSIZE, &limit) != 0) return 5;
    const int fd = open(At("probe.out"), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return 6;
    char line[64];
    const int length = std::snprintf(line, sizeof(line), "fsize=%llu\n", static_cast<unsigned long long>(limit.rlim_cur));
    const bool written = length > 0 && write(fd, line, static_cast<size_t>(length)) == length;
    return close(fd) == 0 && written ? 0 : 7;
}
