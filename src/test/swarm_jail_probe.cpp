// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// Helper for the macOS swarm confinement tests. The tests copy it into a
// confinement root and run it through SwarmRunConfined or
// SwarmRunFilesystemJail with no arguments. It records what it can do in
// <its directory>/probe.out. A marker file in its directory selects a mode:
// alloc.mode touches 4 GiB, spin.mode burns 20 s of CPU, spin_ignore.mode
// does the same with SIGXCPU ignored. Each mode writes <mode>.started before
// and <mode>.done after its work, so a test can tell a stopped child from one
// that never ran or ran to the end. Without a mode it also records whether it
// can look up a Mach service, fork, signal its parent, create POSIX shared
// memory, and whether it starts in its own directory.

#include <arpa/inet.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <netinet/in.h>
#include <servers/bootstrap.h>
#include <signal.h>
#include <sys/mman.h>
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

void Mark(const std::string& path)
{
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) {
        close(fd);
    }
}

bool MachLookup()
{
    mach_port_t port = MACH_PORT_NULL;
    if (bootstrap_look_up(bootstrap_port, "com.apple.system.notification_center", &port) != KERN_SUCCESS) {
        return false;
    }
    mach_port_deallocate(mach_task_self(), port);
    return true;
}

bool Forks()
{
    const pid_t pid = fork();
    if (pid == 0) {
        _exit(0);
    }
    if (pid < 0) {
        return false;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return true;
}

bool PosixShm()
{
    const int fd = shm_open("/btx-swarm-probe", O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        return false;
    }
    close(fd);
    shm_unlink("/btx-swarm-probe");
    return true;
}

bool StartsIn(const std::string& dir)
{
    const int fd = open("cwd.probe", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return access((dir + "/cwd.probe").c_str(), F_OK) == 0;
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
        Mark(dir + "/alloc.started");
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
        Mark(dir + "/alloc.done");
        return 4;
    }
    const bool ignore_sigxcpu = Opens(dir + "/spin_ignore.mode", O_RDONLY);
    if (ignore_sigxcpu || Opens(dir + "/spin.mode", O_RDONLY)) {
        if (ignore_sigxcpu) {
            signal(SIGXCPU, SIG_IGN);
        }
        Mark(dir + "/spin.started");
        volatile unsigned long long n = 0;
        const time_t until = time(nullptr) + 20;
        while (time(nullptr) < until) {
            n = n + 1;
        }
        Mark(dir + "/spin.done");
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
    std::fprintf(out, "mach_lookup=%d\n", MachLookup() ? 1 : 0);
    std::fprintf(out, "fork=%d\n", Forks() ? 1 : 0);
    std::fprintf(out, "signal_parent=%d\n", kill(getppid(), 0) == 0 ? 1 : 0);
    std::fprintf(out, "posix_shm=%d\n", PosixShm() ? 1 : 0);
    std::fprintf(out, "cwd_root=%d\n", StartsIn(dir) ? 1 : 0);
    return std::fclose(out) == 0 ? 0 : 7;
}
