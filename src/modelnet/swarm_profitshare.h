// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_SWARM_PROFITSHARE_H
#define BITCOIN_MODELNET_SWARM_PROFITSHARE_H

#include <cstdint>
#include <string>

namespace modelnet {

struct SwarmProfitShare {
    std::string share_id;
    bool want_share;
};

struct SwarmProfitShareResult {
    std::string code;
    bool shared;
    int64_t atoms;
};

SwarmProfitShareResult SwarmRefuseProfitShare(const SwarmProfitShare& share, std::string& err);

} // namespace modelnet

#endif // BITCOIN_MODELNET_SWARM_PROFITSHARE_H
