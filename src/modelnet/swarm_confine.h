// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BTX_MODELNET_SWARM_CONFINE_H
#define BTX_MODELNET_SWARM_CONFINE_H

#include <util/fs.h>

#include <cstdint>
#include <string>

namespace modelnet {

/**
 * Run a helper under rlimits and a private user and network namespace.
 * This is not a command-string filter. launched stays false unless the
 * helper was actually executed.
 *
 * Before any fork: an empty helper or root is CONFINEMENT; a helper that
 * is not a regular file is SANDBOX_ABSENT; network_denied false is
 * NETWORK; a non-positive address, cpu, or nofile limit is LIMIT; a
 * helper that is not lexically inside root, or a path containing "..",
 * is PATH.
 *
 * The child then sets RLIMIT_AS, RLIMIT_CPU, and RLIMIT_NOFILE, unshares
 * a user namespace and a network namespace, and execs the helper with no
 * extra arguments. Fork failure, or a child exit of 126, is
 * CONFINEMENT_ABSENT. An executed child is CONFINED.
 */
struct SwarmConfineRequest {
    fs::path helper;
    fs::path root;
    int64_t max_address_bytes{0};
    int max_cpu_seconds{0};
    int max_nofile{0};
    bool network_denied{false};
};

struct SwarmConfineResult {
    bool launched{false};
    bool limits_applied{false};
    int child_status{-1};
    std::string code;
};

SwarmConfineResult SwarmRunConfined(const SwarmConfineRequest& request, std::string& err);

} // namespace modelnet

#endif // BTX_MODELNET_SWARM_CONFINE_H
