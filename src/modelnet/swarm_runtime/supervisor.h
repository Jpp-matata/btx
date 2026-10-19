// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license. See COPYING.
#ifndef BTX_MODELNET_SWARM_RUNTIME_SUPERVISOR_H
#define BTX_MODELNET_SWARM_RUNTIME_SUPERVISOR_H
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace modelnet::swarm {
std::filesystem::path ExecutableDirectory();
std::vector<std::string> SanitizeEnvironment(char* const envp[]);
/** Default sidecar lifecycle. Failure never changes chain or wallet state.
 * The override executable exists for local tests/packaging, not an RPC input.
 * This supervisor never launches a shell or an advertised model executable.
 */
class Supervisor final {
public:
    explicit Supervisor(std::filesystem::path modeldir,
                        std::filesystem::path executable = {});
    ~Supervisor();
    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;
    void Stop() noexcept;
private:
    void Run() noexcept;
    std::filesystem::path m_modeldir;
    std::filesystem::path m_executable;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::atomic<bool> m_stop{false};
};
}
#endif
