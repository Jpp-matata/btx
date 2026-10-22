// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_sandbox_macos.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sandbox.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace modelnet {

std::string SwarmSandboxQuote(const fs::path& path)
{
    std::string out{"\""};
    for (const char c : path.native()) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    out += '"';
    return out;
}

std::string SwarmSandboxNetworkDeniedProfile()
{
    // (deny file-ioctl): the helper inherits btxd's stdio. When btxd runs in
    // a terminal, TIOCSTI on that terminal would queue input that the shell
    // reads once btxd exits, i.e. commands run outside the sandbox.
    return "(version 1)\n"
           "(allow default)\n"
           "(deny network*)\n"
           "(deny mach-lookup)\n"
           "(deny file-ioctl)\n";
}

std::string SwarmSandboxConfineProfile()
{
    // (deny signal (require-not (target self))): a confined helper must not
    // be able to signal btxd or any other same-uid process. The confine
    // profile is otherwise (allow default), which leaves signal open; the
    // jail profile already carries the same rule.
    return SwarmSandboxNetworkDeniedProfile() +
           "(deny process-fork)\n"
           "(deny signal (require-not (target self)))\n";
}

std::string SwarmSandboxJailProfile(const fs::path& root)
{
    const std::string r = SwarmSandboxQuote(root);
    // (deny process-fork): a jailed helper could otherwise start processes
    // up to the user's RLIMIT_NPROC (its default, not 1); they stay in the
    // sandbox, so this bounds resource use, not access. The
    // jail sets no rlimits, so this is the only process-count bound it has.
    // (deny ipc-posix*): POSIX shared memory and semaphore names live
    // outside root and outlive the helper; a Linux jail has no /dev/shm.
    return SwarmSandboxNetworkDeniedProfile() +
           "(deny process-fork)\n"
           "(deny ipc-posix*)\n"
           "(deny iokit-open iokit-get-properties)\n"
           "(deny process-info* (require-not (target self)))\n"
           "(deny signal (require-not (target self)))\n"
           "(deny process-exec (require-not (subpath " + r + ")))\n"
           "(deny file-read* file-write* (require-not (require-any\n"
           "    (subpath " + r + ")\n"
           "    (subpath \"/usr/lib\")\n"
           "    (subpath \"/System/Library\")\n"
           "    (subpath \"/System/Cryptexes\")\n"
           "    (subpath \"/System/Volumes/Preboot/Cryptexes\")\n"
           "    (literal \"/dev/null\")\n"
           "    (literal \"/dev/random\")\n"
           "    (literal \"/dev/urandom\")\n"
           "    (literal \"/dev/dtracehelper\")\n"
           "    (literal \"/\"))))\n";
}

bool SwarmSandboxApply(const std::string& profile)
{
    char* error{nullptr};
    // sandbox_init is marked deprecated but still applies the kernel
    // sandbox that sandbox-exec(1) applies. Flags 0: `profile` is SBPL
    // source; a failure leaves the process unconfined and returns -1.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const int rc = sandbox_init(profile.c_str(), 0, &error);
    if (error != nullptr) {
        sandbox_free_error(error);
    }
#pragma clang diagnostic pop
    return rc == 0;
}

bool SwarmSandboxNetworkProvenDenied()
{
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return errno == EPERM || errno == EACCES;
    }
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int rc = bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    const int bind_errno = errno;
    close(fd);
    return rc != 0 && (bind_errno == EPERM || bind_errno == EACCES);
}

} // namespace modelnet
