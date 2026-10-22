// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// Helper for the macOS swarm confinement tests. The tests copy it into a
// confinement root and run it through SwarmRunConfined or
// SwarmRunFilesystemJail with no arguments. It records what it can do in
// <its directory>/probe.out, after creating <its directory>/started in every
// mode. A marker file in its directory selects a mode:
// alloc.mode touches memory until stopped, spin.mode burns CPU until stopped,
// spinignore.mode does the same with SIGXCPU ignored, tty.mode tries
// TIOCSTI on stdin.

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <netinet/in.h>
#include <servers/bootstrap.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
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

// A Mach service every macOS has (notifyd). The profiles deny every lookup.
bool MachLookupAllowed()
{
    mach_port_t port = MACH_PORT_NULL;
    const kern_return_t kr = bootstrap_look_up(bootstrap_port, "com.apple.system.notification_center", &port);
    if (kr == KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), port);
    }
    return kr == KERN_SUCCESS;
}

bool HardwareUuidReadable()
{
    const io_service_t service = IOServiceGetMatchingService(MACH_PORT_NULL, IOServiceMatching("IOPlatformExpertDevice"));
    if (service == 0) {
        return false;
    }
    const CFTypeRef uuid = IORegistryEntryCreateCFProperty(service, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0);
    IOObjectRelease(service);
    if (uuid == nullptr) {
        return false;
    }
    CFRelease(uuid);
    return true;
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
    // Written first in every mode, so a test can tell a helper that ran
    // from one that was killed or refused before exec.
    if (FILE* started = std::fopen((dir + "/started").c_str(), "w")) {
        std::fclose(started);
    }
    int inherited = 0;
    for (int fd = STDERR_FILENO + 1; fd < 1024; ++fd) {
        if (fcntl(fd, F_GETFD) != -1) {
            ++inherited;
        }
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
    // tty.mode: the test made stdin a pty that is not this process's
    // controlling terminal, so TIOCSTI cannot reach a real terminal. The
    // tty driver refuses it with EACCES; the sandbox refuses it with EPERM.
    int tiocsti_errno = -1;
    if (Opens(dir + "/tty.mode", O_RDONLY)) {
        const char c = '#';
        tiocsti_errno = ioctl(STDIN_FILENO, TIOCSTI, &c) == 0 ? 0 : errno;
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
    // Signal 0 only checks permission: the parent is the test process.
    std::fprintf(out, "signal_parent=%d\n", kill(getppid(), 0) == 0 ? 1 : 0);
    std::fprintf(out, "inherited_fds=%d\n", inherited);
    std::fprintf(out, "hardware_uuid=%d\n", HardwareUuidReadable() ? 1 : 0);
    std::fprintf(out, "mach_lookup=%d\n", MachLookupAllowed() ? 1 : 0);
    std::fprintf(out, "tiocsti_errno=%d\n", tiocsti_errno);
    const std::string shm_name = "/btx-swarm-probe-" + std::to_string(getpid());
    const int shm = shm_open(shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm >= 0) {
        close(shm);
        shm_unlink(shm_name.c_str());
    }
    std::fprintf(out, "posix_shm=%d\n", shm >= 0 ? 1 : 0);
    // sysv.id: a System V segment the test created outside the helper.
    // The helper must not attach to it. It creates none itself: creation is
    // not mediated by the sandbox and the system table is small.
    int sysv_attach = -1;
    if (FILE* id_file = std::fopen((dir + "/sysv.id").c_str(), "r")) {
        int id = -1;
        if (std::fscanf(id_file, "%d", &id) == 1) {
            void* segment = shmat(id, nullptr, SHM_RDONLY);
            sysv_attach = segment != reinterpret_cast<void*>(-1) ? 1 : 0;
            if (sysv_attach == 1) {
                shmdt(segment);
            }
        }
        std::fclose(id_file);
    }
    std::fprintf(out, "sysv_attach=%d\n", sysv_attach);
    char* cwd = getcwd(nullptr, 0);
    std::fprintf(out, "cwd_root=%d\n", cwd != nullptr && dir == cwd ? 1 : 0);
    std::free(cwd);
    return std::fclose(out) == 0 ? 0 : 7;
}
