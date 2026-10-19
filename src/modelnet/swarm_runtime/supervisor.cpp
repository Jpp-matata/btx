// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license. See COPYING.
#include "supervisor.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace modelnet::swarm {
std::filesystem::path ExecutableDirectory()
{
    std::array<char, 4096> path{};
#ifdef __linux__
    auto n = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (n > 0) return std::filesystem::path(std::string(path.data(), n)).parent_path();
#elif defined(__APPLE__)
    uint32_t size = path.size();
    if (_NSGetExecutablePath(path.data(), &size) == 0)
        return std::filesystem::weakly_canonical(path.data()).parent_path();
#endif
    return {};
}

std::vector<std::string> SanitizeEnvironment(char* const envp[])
{
    // An allowlist is deliberately narrower than searching for "wallet".
    static const std::set<std::string> allowed{
        "HOME", "PATH", "LANG", "LC_ALL", "LC_CTYPE", "TZ", "TMPDIR", "USER", "LOGNAME",
        "SYSTEMROOT", "WINDIR", "TEMP", "TMP", "CUDA_VISIBLE_DEVICES", "HIP_VISIBLE_DEVICES"
    };
    std::vector<std::string> result;
    if (envp) for (auto p = envp; *p; ++p) {
        const std::string entry(*p);
        const auto equal = entry.find('=');
        if (equal != std::string::npos && allowed.count(entry.substr(0, equal)))
            result.push_back(entry);
    }
    result.emplace_back("PYTHONNOUSERSITE=1");
    result.emplace_back("HF_HUB_OFFLINE=1");
    result.emplace_back("TRANSFORMERS_OFFLINE=1");
    result.emplace_back("DO_NOT_TRACK=1");
    return result;
}

Supervisor::Supervisor(std::filesystem::path modeldir, std::filesystem::path executable)
    : m_modeldir(std::move(modeldir)), m_executable(std::move(executable))
{
    if (m_executable.empty()) m_executable = ExecutableDirectory() / "btx-swarmd";
    m_thread = std::thread([this] { Run(); });
}
Supervisor::~Supervisor() { Stop(); }
void Supervisor::Stop() noexcept
{
    m_stop.store(true); m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void Supervisor::Run() noexcept
{
#ifndef _WIN32
    using namespace std::chrono_literals;
    constexpr std::array<int, 6> delays{1, 2, 5, 10, 30, 60};
    unsigned failures = 0;
    auto wait = [&](int seconds) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_wake.wait_for(lock, std::chrono::seconds(seconds), [&] { return m_stop.load(); });
    };
    try {
        const auto data = m_modeldir / "swarm";
        if (std::filesystem::is_symlink(data)) throw std::runtime_error("swarm data symlink");
        std::filesystem::create_directories(data);
        chmod(data.c_str(), 0700);
        while (!m_stop.load()) {
            pid_t pid = -1;
            if (std::filesystem::is_regular_file(m_executable) && access(m_executable.c_str(), X_OK) == 0) {
                const auto log = data / "supervisor.log";
                int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
                if (fd < 0) throw std::runtime_error("cannot open private swarm log");
                std::vector<std::string> args{m_executable.string(), "--data", data.string(),
                    "--catalog", (m_modeldir / "catalog.json").string()};
                auto env = SanitizeEnvironment(environ);
                env.emplace_back("BTX_SWARM_PARENT_WATCH=1");
                env.emplace_back("BTX_SWARM_PARENT_PID=" + std::to_string(getpid()));
                std::vector<char*> argv, envp;
                for (auto& arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
                for (auto& value : env) envp.push_back(value.data()); envp.push_back(nullptr);
                posix_spawn_file_actions_t actions;
                posix_spawnattr_t attributes;
                const int fa = posix_spawn_file_actions_init(&actions);
                const int at = posix_spawnattr_init(&attributes);
                if (fa || at) {
                    if (!fa) posix_spawn_file_actions_destroy(&actions);
                    if (!at) posix_spawnattr_destroy(&attributes);
                    close(fd); throw std::runtime_error("spawn initialization");
                }
                int error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
                error |= posix_spawn_file_actions_adddup2(&actions, fd, STDOUT_FILENO);
                error |= posix_spawn_file_actions_adddup2(&actions, fd, STDERR_FILENO);
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
                error |= posix_spawn_file_actions_addclosefrom_np(&actions, 3);
#else
                error |= posix_spawn_file_actions_addclose(&actions, fd);
#endif
                short flags = POSIX_SPAWN_SETPGROUP;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
                flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
                error |= posix_spawnattr_setflags(&attributes, flags);
                error |= posix_spawnattr_setpgroup(&attributes, 0);
                if (!error) error = posix_spawn(&pid, m_executable.c_str(), &actions, &attributes, argv.data(), envp.data());
                posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes); close(fd);
                if (error) pid = -1;
            }
            if (pid <= 0) {
                std::fprintf(stderr, "BTX swarm sidecar unavailable; monetary operation is unchanged\n");
                wait(delays[std::min<unsigned>(failures++, delays.size()-1)]); continue;
            }
            const auto started = std::chrono::steady_clock::now();
            int status{}; bool exited = false;
            while (!m_stop.load()) {
                const auto r = waitpid(pid, &status, WNOHANG);
                if (r == pid || (r < 0 && errno == ECHILD)) { exited = true; break; }
                if (r < 0 && errno != EINTR) break;
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait_for(lock, 200ms, [&] { return m_stop.load(); });
            }
            if (!exited) {
                kill(-pid, SIGTERM);
                for (int attempt = 0; attempt < 50; ++attempt) {
                    const auto r = waitpid(pid, &status, WNOHANG);
                    if (r == pid || (r < 0 && errno == ECHILD)) { exited = true; break; }
                    std::this_thread::sleep_for(100ms);
                }
                if (!exited) { kill(-pid, SIGKILL); while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
            }
            if (std::chrono::steady_clock::now() - started > 60s) failures = 0;
            if (!m_stop.load()) wait(delays[std::min<unsigned>(failures++, delays.size()-1)]);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "BTX swarm sidecar stopped: %s; monetary operation is unchanged\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "BTX swarm sidecar stopped; monetary operation is unchanged\n");
    }
#else
    // Named-pipe/ACL and Windows Job Object execution require native qualification.
    // Do not fake a successful subprocess or silently start an unconfined daemon.
    std::fprintf(stderr, "BTX swarm Windows lifecycle is not release-qualified\n");
#endif
}
}
