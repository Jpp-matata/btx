// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <modelnet/swarm_fsjail.h>

#include <fcntl.h>
#if defined(__linux__)
#include <sched.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <modelnet/swarm_sandbox_macos.h>
#endif
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace modelnet {
namespace {

#if defined(__linux__)
constexpr char PUT_OLD_NAME[] = ".btx-fsjail-old";
constexpr char PUT_OLD_IN_JAIL[] = "/.btx-fsjail-old";
constexpr size_t MAP_TEXT_MAX = 64;
#endif

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

bool LexicallyInside(const fs::path& path, const fs::path& root)
{
    if (PathContainsDotDot(path) || PathContainsDotDot(root)) {
        return false;
    }
    const fs::path relative{path.lexically_relative(root)};
    if (relative.empty()) {
        return false;
    }
    if (relative == fs::path(".")) {
        return true;
    }
    return !PathContainsDotDot(relative);
}

SwarmFilesystemJailResult Refuse(const char* code, std::string& err)
{
    SwarmFilesystemJailResult result;
    result.code = code;
    err = code;
    return result;
}

#if defined(__linux__)
bool WriteAll(const char* path, const char* data, size_t length)
{
    const int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    size_t written = 0;
    while (written < length) {
        const ssize_t n = write(fd, data + written, length - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return close(fd) == 0;
}

bool MapUser(const char* uid_map, int uid_len, const char* gid_map, int gid_len)
{
    if (uid_map == nullptr || gid_map == nullptr || uid_len <= 0 || gid_len <= 0 ||
        static_cast<size_t>(uid_len) >= MAP_TEXT_MAX || static_cast<size_t>(gid_len) >= MAP_TEXT_MAX) {
        return false;
    }
    if (!WriteAll("/proc/self/setgroups", "deny", 4)) {
        return false;
    }
    if (!WriteAll("/proc/self/uid_map", uid_map, static_cast<size_t>(uid_len))) {
        return false;
    }
    return WriteAll("/proc/self/gid_map", gid_map, static_cast<size_t>(gid_len));
}

#endif

bool DirectoryOpenable(const char* path)
{
    const int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

fs::path SelectWitness(const fs::path& root)
{
    auto consider = [&](const fs::path& candidate) -> fs::path {
        if (candidate.empty() || !candidate.is_absolute() || candidate == fs::path("/")) {
            return {};
        }
        if (candidate == root || LexicallyInside(candidate, root)) {
            return {};
        }
        if (!DirectoryOpenable(candidate.c_str())) {
            return {};
        }
        return candidate;
    };

    const fs::path parent = consider(root.parent_path());
    if (!parent.empty()) {
        return parent;
    }
    const char* fallbacks[] = {"/proc", "/sys", "/dev", "/etc", "/usr", "/bin", "/lib"};
    for (const char* fallback : fallbacks) {
        const fs::path found = consider(fs::path(fallback));
        if (!found.empty()) {
            return found;
        }
    }
    return {};
}

#if defined(__linux__)
int PivotRoot(const char* new_root, const char* put_old)
{
    return syscall(SYS_pivot_root, new_root, put_old);
}

bool PivotToRoot(const fs::path& root, const fs::path& put_old)
{
    if (mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
        return false;
    }
    if (mount(root.c_str(), root.c_str(), nullptr, MS_BIND | MS_REC, nullptr) != 0) {
        return false;
    }
    if (mkdir(put_old.c_str(), 0700) != 0 && errno != EEXIST) {
        return false;
    }
    if (PivotRoot(root.c_str(), put_old.c_str()) != 0) {
        return false;
    }
    if (chdir("/") != 0) {
        return false;
    }
    if (umount2(PUT_OLD_IN_JAIL, MNT_DETACH) != 0) {
        return false;
    }
    rmdir(PUT_OLD_IN_JAIL);
    return !DirectoryOpenable(PUT_OLD_IN_JAIL);
}

bool ChrootToRoot(const fs::path& root)
{
    if (chdir(root.c_str()) != 0) {
        return false;
    }
    if (chroot(".") != 0) {
        return false;
    }
    return chdir("/") == 0;
}

bool ConfinedToRoot(const fs::path& root, const fs::path& put_old, const fs::path& witness)
{
    // The witness was opened in the parent. If it still opens after
    // pivot_root or chroot, this process can see a path outside root.
    if (!DirectoryOpenable(witness.c_str())) {
        return false;
    }
    const bool pivoted = PivotToRoot(root, put_old);
    if (!pivoted && !ChrootToRoot(root)) {
        return false;
    }
    if (DirectoryOpenable(witness.c_str())) {
        return false;
    }
    if (pivoted && DirectoryOpenable(PUT_OLD_IN_JAIL)) {
        return false;
    }
    return true;
}
#endif

void CloseInheritedDescriptors()
{
    for (int fd = STDERR_FILENO + 1; fd < 1024; ++fd) {
        close(fd);
    }
}

} // namespace

SwarmFilesystemJailResult SwarmRunFilesystemJail(const SwarmFilesystemJailRequest& request, std::string& err)
{
    if (request.helper.empty() || request.root.empty()) {
        return Refuse("JAIL", err);
    }
    if (!fs::exists(request.helper) || !fs::is_regular_file(request.helper)) {
        return Refuse("SANDBOX_ABSENT", err);
    }
    if (!request.network_denied) {
        return Refuse("NETWORK", err);
    }
    if (!HelperInsideRoot(request.helper, request.root)) {
        return Refuse("PATH", err);
    }

    const fs::path relative{request.helper.lexically_relative(request.root)};
#if defined(__linux__)
    const fs::path root = request.root;
    const fs::path put_old = root / PUT_OLD_NAME;
    const fs::path witness = SelectWitness(root);
    const fs::path jailed_helper = fs::path("/") / relative;
    const bool helper_in_jail = !jailed_helper.empty() && jailed_helper != fs::path("/");
    char uid_map[MAP_TEXT_MAX];
    char gid_map[MAP_TEXT_MAX];
    const int uid_len = snprintf(uid_map, sizeof(uid_map), "0 %lu 1\n", static_cast<unsigned long>(geteuid()));
    const int gid_len = snprintf(gid_map, sizeof(gid_map), "0 %lu 1\n", static_cast<unsigned long>(getegid()));
#elif defined(__APPLE__)
    // macOS has no mount namespace. The kernel sandbox limits the
    // filesystem to root instead (see SwarmSandboxJailProfile). Seatbelt
    // matches resolved paths, so root is canonicalized first; the helper is
    // exec'd by its path below the canonical root, and a symlink that leaves
    // root is refused at exec. The witness (a directory outside root that
    // opens now) must stop opening once the sandbox is applied.
    std::error_code canonical_error;
    const fs::path root = fs::canonical(request.root, canonical_error);
    if (canonical_error || root.empty()) {
        return Refuse("JAIL_ABSENT", err);
    }
    const fs::path witness = SelectWitness(root);
    const fs::path jailed_helper = root / relative;
    const std::string profile = SwarmSandboxJailProfile(root);
#else
    const fs::path jailed_helper = request.helper;
#endif

    const pid_t pid = fork();
    if (pid < 0) {
        return Refuse("JAIL_ABSENT", err);
    }
    if (pid == 0) {
#if defined(__linux__)
        // User namespace, then mount namespace. Either failure exits
        // before exec. A successful pair still does not prove that root
        // is a private mount.
        if (unshare(CLONE_NEWUSER) != 0) {
            _exit(126);
        }
        if (unshare(CLONE_NEWNS) != 0) {
            _exit(126);
        }
        if (!MapUser(uid_map, uid_len, gid_map, gid_len) || !helper_in_jail ||
            !ConfinedToRoot(root, put_old, witness)) {
            _exit(126);
        }
#elif defined(__APPLE__)
        if (witness.empty() || !DirectoryOpenable(witness.c_str()) || !SwarmSandboxApply(profile) ||
            DirectoryOpenable(witness.c_str()) || !SwarmSandboxNetworkProvenDenied()) {
            _exit(126);
        }
        // On Linux the helper starts in the jail's "/". Start it in root
        // here too, so that a relative path resolves inside root.
        if (chdir(root.c_str()) != 0) {
            _exit(126);
        }
#else
        // No filesystem jail on this platform: never exec unjailed.
        _exit(126);
#endif
        CloseInheritedDescriptors();
        char* argv[] = {const_cast<char*>(jailed_helper.c_str()), nullptr};
        execv(jailed_helper.c_str(), argv);
        _exit(126);
    }

    int status = 0;
    pid_t waited = -1;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) == 126) {
        return Refuse("JAIL_ABSENT", err);
    }

    SwarmFilesystemJailResult result;
    result.launched = true;
    result.jailed = true;
    result.code = "JAILED";
    err.clear();
    return result;
}

} // namespace modelnet
