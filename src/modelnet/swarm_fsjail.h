// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BTX_MODELNET_SWARM_FSJAIL_H
#define BTX_MODELNET_SWARM_FSJAIL_H

#include <util/fs.h>

#include <string>

namespace modelnet {

/**
 * Run a helper inside a filesystem root the child cannot see past.
 * launched and jailed stay false unless this function created that jail
 * and exec'd the helper in it. A refused setup is not a jail.
 *
 * Before any fork: an empty helper or root is JAIL; a helper that is not
 * a regular file is SANDBOX_ABSENT; network_denied false is NETWORK; a
 * helper path containing ".." or not lexically inside root is PATH.
 *
 * The child unshares a user namespace, then a mount namespace. Either
 * failure exits 126 without exec. A private mount of root still has to be
 * proven: the child execs only after pivot_root or chroot succeeds and a
 * path outside root can no longer be opened. Otherwise it exits 126
 * without exec. The parent reports that 126, a signal, or a fork failure
 * as JAIL_ABSENT. Only an exec after that proof is JAILED.
 */
struct SwarmFilesystemJailRequest {
    fs::path helper;
    fs::path root;
    bool network_denied{false};
};

struct SwarmFilesystemJailResult {
    bool launched{false};
    bool jailed{false};
    std::string code;
};

SwarmFilesystemJailResult SwarmRunFilesystemJail(const SwarmFilesystemJailRequest& request, std::string& err);

} // namespace modelnet

#endif // BTX_MODELNET_SWARM_FSJAIL_H
