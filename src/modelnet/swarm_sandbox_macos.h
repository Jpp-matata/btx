// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BTX_MODELNET_SWARM_SANDBOX_MACOS_H
#define BTX_MODELNET_SWARM_SANDBOX_MACOS_H

#include <util/fs.h>

#include <string>

namespace modelnet {

/**
 * macOS confinement for the swarm helpers (Seatbelt, the kernel sandbox
 * behind sandbox-exec). Linux uses namespaces instead; see swarm_confine
 * and swarm_fsjail.
 *
 * Profiles are built in the parent before fork; the child only applies one
 * with SwarmSandboxApply and then proves it before exec. A sandbox cannot
 * be lifted by the confined process and is inherited across exec and fork.
 */

/** Every network operation and every Mach service lookup denied. */
std::string SwarmSandboxNetworkDeniedProfile();

/**
 * The network-denied profile plus a filesystem limited to `root`: reads,
 * writes and exec only inside root. Readable outside root are only the
 * read-only system runtime a dynamically linked macOS binary needs (dyld
 * shared cache and system libraries), /dev/null, /dev/random,
 * /dev/urandom, /dev/dtracehelper and the directory "/" itself. IOKit
 * clients and other processes (info, signals) are denied. `root` must be
 * canonical: the kernel matches resolved paths.
 */
std::string SwarmSandboxJailProfile(const fs::path& root);

/** Apply `profile` to this process. False if the kernel refused it. */
bool SwarmSandboxApply(const std::string& profile);

/** True only if a loopback UDP bind is refused (EPERM/EACCES). */
bool SwarmSandboxNetworkProvenDenied();

/** `path` as an SBPL string literal (quotes and backslashes escaped). */
std::string SwarmSandboxQuote(const fs::path& path);

} // namespace modelnet

#endif // BTX_MODELNET_SWARM_SANDBOX_MACOS_H
