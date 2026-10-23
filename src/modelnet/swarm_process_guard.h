// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license. See COPYING.
#ifndef BITCOIN_MODELNET_SWARM_PROCESS_GUARD_H
#define BITCOIN_MODELNET_SWARM_PROCESS_GUARD_H

#include <util/fs.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
#ifdef __APPLE__
#include <dlfcn.h>
#endif
#endif

namespace modelnet::process_guard {

/** Check every path component. canonical() alone would silently follow links. */
inline bool NoSymlinkPath(const fs::path& path)
{
    if (!path.is_absolute()) return false;
#ifdef _WIN32
    // Reject UNC/device namespaces, alternate data streams and all reparse
    // points (junctions are broader than std::filesystem symlinks).
    const auto spelling=path.native();
    if(spelling.size()<3 || spelling[1]!=L':' || spelling.find(L':',2)!=std::wstring::npos ||
       spelling.starts_with(L"\\"))return false;
#endif
    std::error_code ec;
    fs::path cursor;
    for (const auto& component : path) {
        if (component == "..") return false;
        cursor /= component;
#ifdef _WIN32
        if(!cursor.is_absolute())continue;
        const DWORD attributes=GetFileAttributesW(cursor.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
#endif
        const auto status = fs::symlink_status(cursor, ec);
        if (ec || fs::is_symlink(status)) return false;
    }
    return true;
}

inline bool InsideRoot(const fs::path& helper, const fs::path& root)
{
    if (!NoSymlinkPath(root) || !NoSymlinkPath(helper)) return false;
    std::error_code ec;
    if (!fs::is_directory(root, ec) || ec || !fs::is_regular_file(helper, ec) || ec) return false;
    const fs::path relative{helper.lexically_normal().lexically_relative(root.lexically_normal())};
    if (relative.empty() || relative == fs::path(".") || relative.is_absolute()) return false;
    for (const auto& part : relative) if (part == "..") return false;
    return true;
}

#ifndef _WIN32
inline bool Limit(int resource, rlim_t value)
{
    rlimit current{};
    if (getrlimit(resource, &current) != 0) return false;
    const rlim_t bounded = std::min(value, current.rlim_max);
    const rlimit limit{bounded, bounded};
    return bounded != 0 && setrlimit(resource, &limit) == 0;
}

inline bool ErrorPipe(int (&fds)[2])
{
    if (pipe(fds) != 0) return false;
    for (int fd : fds) {
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
            close(fds[0]); close(fds[1]); return false;
        }
    }
    return true;
}

[[noreturn]] inline void ChildFailure(int fd)
{
    int error = errno == 0 ? EACCES : errno;
    const auto* bytes = reinterpret_cast<const char*>(&error);
    size_t written = 0;
    while (written < sizeof(error)) {
        const ssize_t n = write(fd, bytes + written, sizeof(error) - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        written += static_cast<size_t>(n);
    }
    _exit(126);
}

/** No inherited wallet/RPC descriptors or process environment survive exec. */
inline bool ChildReady(int fd)
{
    const int ready = 0;
    ssize_t n;
    do { n = write(fd, &ready, sizeof(ready)); } while (n < 0 && errno == EINTR);
    return n == static_cast<ssize_t>(sizeof(ready));
}

inline bool CloseDescriptors(int error_fd, long maximum)
{
    if (error_fd != 3) {
        if (dup2(error_fd, 3) < 0) return false;
        close(error_fd);
    }
    if (fcntl(3, F_SETFD, FD_CLOEXEC) < 0) return false;
#ifdef __linux__
#ifdef SYS_close_range
    if (syscall(SYS_close_range, 4U, ~0U, 0U) == 0) return true;
    if (errno != ENOSYS && errno != EINVAL) return false;
#endif
#endif
    // maximum is captured in the parent before reducing RLIMIT_NOFILE.
    for (long fd = 4; fd < maximum; ++fd) close(static_cast<int>(fd));
    return true;
}

inline bool NullStandardDescriptors()
{
    const int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    bool ok = true;
    for (int target = 0; target < 3; ++target) if (dup2(fd, target) < 0) ok = false;
    if (fd > 2) close(fd);
    return ok;
}

/**
 * True while the regular files below root total at most max_bytes (logical
 * sizes; links are not followed). Any error reading the tree, such as a
 * directory the helper made unreadable, counts as over the limit. With
 * running set, an entry removed between listing and reading it is skipped:
 * a helper that is still running may rename or delete its own files.
 */
inline bool TreeWithin(const fs::path& root, uint64_t max_bytes, bool running = false)
{
    std::error_code ec;
    uint64_t total = 0;
    const auto vanished = [&] { return running && ec == std::errc::no_such_file_or_directory; };
    fs::recursive_directory_iterator current(root, fs::directory_options::none, ec), end;
    if (ec) return false;
    for (; current != end; current.increment(ec)) {
        if (ec) return false;
        const auto status = current->symlink_status(ec);
        if (vanished()) {
            current.disable_recursion_pending();
            continue;
        }
        if (ec) return false;
        if (!fs::is_regular_file(status)) continue;
        const uintmax_t bytes = current->file_size(ec);
        if (vanished()) continue;
        if (ec || bytes > max_bytes - total) return false;
        total += bytes;
    }
    return !ec;
}

struct WaitResult {
    bool executed{false};
    bool timed_out{false};
    bool over_tree{false};
    int status{-1};
    int setup_errno{0};
};

/**
 * max_tree_bytes (0 = off): while the child runs, the tree below tree_root is
 * checked with TreeWithin every 10 ms, and the child's process group gets
 * SIGKILL once it is over (over_tree; the signal reads as not executed). A
 * failed walk is repeated once before the kill, since a directory removed
 * during a walk ends it early. A child can overshoot by what it writes
 * within one interval; a walk reads every entry below tree_root, so a large
 * tree lengthens the interval.
 */
inline WaitResult Wait(pid_t child, int error_fd, int wall_seconds, const fs::path& tree_root = {},
                       uint64_t max_tree_bytes = 0)
{
    WaitResult result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(wall_seconds);
    for (bool first = true;; first = false) {
        const pid_t done = waitpid(child, &result.status, WNOHANG);
        if (done == child) break;
        if (done < 0 && errno != EINTR) { close(error_fd); return result; }
        // Not on the first pass: the caller checked the tree just before launch.
        if (!first && max_tree_bytes != 0 && !TreeWithin(tree_root, max_tree_bytes, true) &&
            !TreeWithin(tree_root, max_tree_bytes, true)) {
            result.over_tree = true;
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            // Only the dedicated process group created by this invocation.
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
            while (waitpid(child, &result.status, 0) < 0 && errno == EINTR) {}
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    int messages[2]{-1, -1};
    size_t got = 0;
    while (got < sizeof(messages)) {
        const ssize_t n = read(error_fd, reinterpret_cast<char*>(messages) + got, sizeof(messages) - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    close(error_fd);
    result.setup_errno = got >= sizeof(int) && messages[0] != 0 ? messages[0] :
                         got == sizeof(messages) ? messages[1] : 0;
    // A ready marker followed by CLOEXEC EOF and normal exit proves setup
    // completed and exec did not report failure. Signals are ambiguous.
    result.executed = got == sizeof(int) && messages[0] == 0 &&
                      WIFEXITED(result.status) && !result.timed_out;
    return result;
}

#ifdef __APPLE__
/** The SDK's removed named sandbox profiles are deliberately not used. */
class Seatbelt {
    using Init = int (*)(const char*, uint64_t, const char* const*, char**);
    using FreeError = void (*)(char*);
    using Check = int (*)(pid_t, const char*, int, ...);
    void* m_library{nullptr};
    Init m_init{nullptr};
    FreeError m_free{nullptr};
    Check m_check{nullptr};
public:
    Seatbelt()
    {
        m_library = dlopen("/usr/lib/libsandbox.dylib", RTLD_NOW | RTLD_LOCAL);
        if (!m_library) return;
        m_init = reinterpret_cast<Init>(dlsym(m_library, "sandbox_init_with_parameters"));
        m_free = reinterpret_cast<FreeError>(dlsym(m_library, "sandbox_free_error"));
        m_check = reinterpret_cast<Check>(dlsym(m_library, "sandbox_check"));
    }
    ~Seatbelt() { if (m_library) dlclose(m_library); }
    bool Available() const { return m_init && m_free && m_check; }
    bool Apply(const char* root, const char* helper) const
    {
        if (!Available()) return false;
        static constexpr const char* profile =
            "(version 1)\n(deny default)\n"
            "(deny network*)\n"
            "(allow process-exec (literal (param \"HELPER\")))\n"
            "(allow file-read* (subpath (param \"ROOT\"))"
            " (subpath \"/System/Library\") (subpath \"/usr/lib\")"
            " (literal \"/dev/null\") (literal \"/dev/urandom\") (literal \"/dev/random\"))\n"
            "(allow file-write* (subpath (param \"ROOT\")) (literal \"/dev/null\"))\n"
            "(allow sysctl-read)\n";
        const char* params[]{"ROOT", root, "HELPER", helper, nullptr};
        char* error = nullptr;
        const int rc = m_init(profile, 0, params, &error);
        if (error) m_free(error);
        return rc == 0 && m_check(getpid(), nullptr, 0) == 1;
    }
};
#endif
#endif // !_WIN32
} // namespace modelnet::process_guard
#endif
