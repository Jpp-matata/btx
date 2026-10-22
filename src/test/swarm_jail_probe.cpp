// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// Helper for the macOS swarm confinement tests. The tests copy it into a
// confinement root and run it through SwarmRunConfined or
// SwarmRunFilesystemJail with no arguments. It records what it can do in
// <its directory>/probe.out. A marker file in its directory selects a mode:
// alloc.mode touches memory until stopped, spin.mode burns CPU until stopped,
// spinignore.mode does the same with SIGXCPU ignored.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace {

bool Opens(const std::string& path, int flags)
{
    const int fd = open(path.c_str(), flags | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

bool LoopbackBind()
{
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool bound = bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
    close(fd);
    return bound;
}

unsigned long long Limit(int resource)
{
    rlimit limit{};
    return getrlimit(resource, &limit) == 0 ? static_cast<unsigned long long>(limit.rlim_cur) : 0;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string self = argc > 0 && argv[0] != nullptr ? argv[0] : "";
    const std::string dir = self.substr(0, self.rfind('/'));
    if (dir.empty()) {
        return 2;
    }
    if (Opens(dir + "/alloc.mode", O_RDONLY)) {
        // Volatile page writes: the compiler may not drop an allocation
        // that is never read (it would, with malloc and memset alone).
        for (int i = 0; i < 256; ++i) {
            volatile char* block = static_cast<volatile char*>(std::malloc(16 << 20));
            if (block == nullptr) {
                return 3;
            }
            for (int page = 0; page < (16 << 20); page += 4096) {
                block[page] = 1;
            }
        }
        sleep(5);
        return 4;
    }
    const bool ignore_xcpu = Opens(dir + "/spinignore.mode", O_RDONLY);
    if (ignore_xcpu) {
        signal(SIGXCPU, SIG_IGN);
    }
    if (ignore_xcpu || Opens(dir + "/spin.mode", O_RDONLY)) {
        volatile unsigned long long n = 0;
        const time_t until = time(nullptr) + 20;
        while (time(nullptr) < until) {
            n = n + 1;
        }
        return 5;
    }
    FILE* out = std::fopen((dir + "/probe.out").c_str(), "w");
    if (out == nullptr) {
        return 6;
    }
    std::fprintf(out, "outside_dir=%d\n", Opens(dir + "/..", O_RDONLY | O_DIRECTORY) ? 1 : 0);
    std::fprintf(out, "etc_hosts=%d\n", Opens("/private/etc/hosts", O_RDONLY) ? 1 : 0);
    std::fprintf(out, "loopback_bind=%d\n", LoopbackBind() ? 1 : 0);
    std::fprintf(out, "nofile=%llu\n", Limit(RLIMIT_NOFILE));
    std::fprintf(out, "cpu=%llu\n", Limit(RLIMIT_CPU));
    const pid_t child = fork();
    if (child == 0) {
        _exit(0);
    }
    if (child > 0) {
        int status = 0;
        waitpid(child, &status, 0);
    }
    std::fprintf(out, "fork=%d\n", child > 0 ? 1 : 0);
    return std::fclose(out) == 0 ? 0 : 7;
}
