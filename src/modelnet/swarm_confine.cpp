// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <modelnet/swarm_confine.h>

#include <sched.h>
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
    return setrlimit(resource, &limit) == 0;
}

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

    const pid_t pid = fork();
    if (pid < 0) {
        return Refuse("CONFINEMENT_ABSENT", err);
    }
    if (pid == 0) {
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
        char* argv[] = {const_cast<char*>(request.helper.c_str()), nullptr};
        execv(request.helper.c_str(), argv);
        _exit(126);
    }

    int status = 0;
    pid_t waited = -1;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
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
