// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_CAP02_CAP02_H
#define BITCOIN_MODELNET_CAP02_CAP02_H

#include <modelnet/nifdt_contract/contract.h>

#include <string>

namespace modelnet {

/**
 * Cap02. ASR-CAP-002 reads manifest.txt from a caller-supplied directory.
 * Each non-empty line is "name hex16". The named file must sit in that
 * directory and its FNV-1a 64-bit hex must equal hex16. A matching manifest
 * does not qualify a release. A temporary directory is not a release
 * environment.
 * passed(), release_qualified(), gpu_passed(), windows_passed(), and
 * hardware_qualified() stay false.
 * automatic_spend() is NIFDT_AUTOMATIC_SPEND.
 * This class opens no socket and starts no process.
 */
class Cap02 {
public:
    bool Qualify(const std::string& dir, std::string& err);

    int files_checked() const { return m_files; }
    bool passed() const { return false; }
    bool release_qualified() const { return false; }
    bool gpu_passed() const { return false; }
    bool windows_passed() const { return false; }
    bool hardware_qualified() const { return false; }
    std::string automatic_spend() const { return NIFDT_AUTOMATIC_SPEND; }

private:
    int m_files{0};
};

} // namespace modelnet

#endif // BITCOIN_MODELNET_CAP02_CAP02_H
