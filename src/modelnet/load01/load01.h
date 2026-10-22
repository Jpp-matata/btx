// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_LOAD01_LOAD01_H
#define BITCOIN_MODELNET_LOAD01_LOAD01_H

#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>

namespace modelnet {

/**
 * Load01. ASR-ART-005 reads one verified checkpoint from a caller-supplied
 * path. The file is hashed with FNV-1a 64-bit and those 16 hex digits are
 * compared with digest_hex. An empty path or an empty digest is FIELD and
 * reads nothing. A missing path is ABSENT. A directory or an unreadable
 * path is IO. A digest mismatch is DIGEST and the bytes are not kept. A
 * match stores the size and loaded() becomes true. A later Load of a
 * different path is SOURCE and the first record stays. One source is not
 * a second source. passed(), release_qualified(), gpu_passed(),
 * windows_passed(), and hardware_qualified() stay false.
 * automatic_spend() is NIFDT_AUTOMATIC_SPEND. hosts() is 1.
 */
class Load01 {
public:
    bool Load(const std::string& source_path, const std::string& digest_hex, std::string& err);

    bool loaded() const { return m_loaded; }
    int64_t bytes_read() const { return m_bytes; }
    int64_t hosts() const { return static_cast<int64_t>(1); }

    bool passed() const { return false; }
    bool release_qualified() const { return false; }
    bool gpu_passed() const { return false; }
    bool windows_passed() const { return false; }
    bool hardware_qualified() const { return false; }
    std::string automatic_spend() const { return NIFDT_AUTOMATIC_SPEND; }

private:
    bool m_loaded{false};
    int64_t m_bytes{0};
    std::string m_source;
};

} // namespace modelnet

#endif // BITCOIN_MODELNET_LOAD01_LOAD01_H
