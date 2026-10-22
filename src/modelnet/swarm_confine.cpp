// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <modelnet/swarm_confine.h>

#if defined(__linux__)
#include <sched.h>
#elif defined(__APPLE__)
#include <modelnet/swarm_sandbox_macos.h>

#include <libproc.h>
#include <signal.h>
#include <time.h>
#endif
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>

namespace modelnet {
namespace {

bool PathContainsDotDot(const fs::path& path)
{
    for (const auto& part : path) {
        if (part == "..") {
            return true;
        }
    }
    return false;
}

bool HelperInsideRoot(const fs::path& helper, const fs::path& root)
{
    if (PathContainsDotDot(helper) || PathContainsDotDot(root)) {
        return false;
    }
    const fs::path relative{helper.lexically_relative(root)};
    if (relative.empty() || relative == fs::path(".")) {
        return false;
    }
    return !PathContainsDotDot(relative);
}

SwarmConfineResult Refuse(const char* code, std::string& err)
{
    SwarmConfineResult result;
    result.code = code;
    err = code;
    return result;
}

bool SetHardLimit(int resource, rlim_t value)
{
    const rlimit limit{value, value};
    if (setrlimit(resource, &limit) != 0) {
        return false;
    }
    rlimit applied{};
    return getrlimit(resource, &applied) == 0 && applied.rlim_cur == value && applied.rlim_max == value;
}

#if defined(__APPLE__)
// macOS does not enforce RLIMIT_AS: setrlimit refuses it with EINVAL and
// the kernel never caps mmap. The parent enforces the address limit on the
// child's physical footprint instead. It samples proc_pid_rusage every
// FOOTPRINT_POLL_NS and sends SIGKILL once the footprint is above the
// limit, so an allocation can overshoot by what the child touches within
// one interval. The status then reports that signal.
constexpr long FOOTPRINT_POLL_NS{10'000'000};

bool FootprintAbove(pid_t pid, uint64_t limit)
{
    rusage_info_v4 usage{};
    if (proc_pid_rusage(pid, RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&usage)) != 0) {
        return false;
    }
    return usage.ri_phys_footprint > limit;
}

pid_t WaitEnforcingFootprint(pid_t pid, int& status, uint64_t limit)
{
    bool killed = false;
    while (true) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited != 0) {
            if (waited < 0 && errno == EINTR) {
                continue;
            }
            return waited;
        }
        if (!killed && FootprintAbove(pid, limit)) {
            kill(pid, SIGKILL);
            killed = true;
        }
        const timespec pause{0, FOOTPRINT_POLL_NS};
        nanosleep(&pause, nullptr);
    }
}
#endif

} // namespace

SwarmConfineResult SwarmRunConfined(const SwarmConfineRequest& request, std::string& err)
{
    if (request.helper.empty() || request.root.empty()) {
        return Refuse("CONFINEMENT", err);
    }
    if (!fs::exists(request.helper) || !fs::is_regular_file(request.helper)) {
        return Refuse("SANDBOX_ABSENT", err);
    }
    if (!request.network_denied) {
        return Refuse("NETWORK", err);
    }
    if (request.max_address_bytes <= 0 || request.max_cpu_seconds <= 0 || request.max_nofile <= 0) {
        return Refuse("LIMIT", err);
    }
    if (!HelperInsideRoot(request.helper, request.root)) {
        return Refuse("PATH", err);
    }
#if defined(__APPLE__)
    // Built before fork: the child only applies it.
    const std::string profile = SwarmSandboxNetworkDeniedProfile();
#endif

    const pid_t pid = fork();
    if (pid < 0) {
        return Refuse("CONFINEMENT_ABSENT", err);
    }
    if (pid == 0) {
#if defined(__linux__)
        if (!SetHardLimit(RLIMIT_AS, static_cast<rlim_t>(request.max_address_bytes)) ||
            !SetHardLimit(RLIMIT_CPU, static_cast<rlim_t>(request.max_cpu_seconds)) ||
            !SetHardLimit(RLIMIT_NOFILE, static_cast<rlim_t>(request.max_nofile))) {
            _exit(126);
        }
        // One unshare creates the user namespace first, then the network
        // namespace. That is the unprivileged equivalent of CLONE_NEWUSER
        // followed by CLONE_NEWNET.
        if (unshare(CLONE_NEWUSER | CLONE_NEWNET) != 0) {
            _exit(126);
        }
#elif defined(__APPLE__)
        // The address limit is enforced by the parent (see
        // WaitEnforcingFootprint). The sandbox denies every network
        // operation and every Mach service lookup (no name resolution
        // through mDNSResponder either), and is proven before exec.
        if (!SetHardLimit(RLIMIT_CPU, static_cast<rlim_t>(request.max_cpu_seconds)) ||
            !SetHardLimit(RLIMIT_NOFILE, static_cast<rlim_t>(request.max_nofile))) {
            _exit(126);
        }
        if (!SwarmSandboxApply(profile) || !SwarmSandboxNetworkProvenDenied()) {
            _exit(126);
        }
#else
        // No confinement primitive on this platform: never exec unconfined.
        _exit(126);
#endif
        char* argv[] = {const_cast<char*>(request.helper.c_str()), nullptr};
        execv(request.helper.c_str(), argv);
        _exit(126);
    }

    int status = 0;
    pid_t waited = -1;
#if defined(__APPLE__)
    waited = WaitEnforcingFootprint(pid, status, static_cast<uint64_t>(request.max_address_bytes));
#else
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
#endif
    if (waited < 0) {
        return Refuse("CONFINEMENT_ABSENT", err);
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 126) {
        return Refuse("CONFINEMENT_ABSENT", err);
    }

    SwarmConfineResult result;
    result.launched = true;
    result.limits_applied = true;
    result.child_status = status;
    result.code = "CONFINED";
    err.clear();
    return result;
}

} // namespace modelnet
