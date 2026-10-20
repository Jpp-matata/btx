// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_hashlock/claim.h>

#include <modelnet/swarm_hashlock/digest.h>

namespace modelnet {

bool SwarmHashlockApplyClaim(SwarmHashlockOffer& offer,
                             const std::vector<unsigned char>& preimage,
                             std::string& err)
{
    if (preimage.size() != kSwarmHashlockDigestSize) {
        err = SWARM_HASHLOCK_PREIMAGE;
        return false;
    }
    // SwarmInvoice also needs an amount, a beneficiary, an objective, and a
    // route. This offer stores only an id and a payment hash, so
    // SwarmBindPreimage is not called. The gate is one SHA-256.
    if (!SwarmHashlockDigestEquals(offer.payment_hash, preimage)) {
        err = SWARM_HASHLOCK_HASHLOCK;
        return false;
    }
    if (offer.claimed) {
        err = SWARM_HASHLOCK_REPLAY;
        return false;
    }
    offer.claimed = true;
    err.clear();
    return true;
}

} // namespace modelnet
