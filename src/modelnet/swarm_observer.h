// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_SWARM_OBSERVER_H
#define BITCOIN_MODELNET_SWARM_OBSERVER_H

#include <util/fs.h>

#include <string>

namespace modelnet {

/** SHA-384 of BTX/ProvisionalModelTopic/1 || 0x00 || {"model_id":"<id>"}. */
std::string SwarmProvisionalLineage(const std::string& model_id);

/**
 * Read modeldir/catalog.json and merge verified, complete models into
 * modeldir/swarm/observers.json. A missing catalog leaves the file unchanged.
 * Subscriptions are observers only: no compute grant, no seeding, no spend.
 * Returns false without writing when the catalog or a mapping is unusable.
 */
bool SwarmObserveCatalog(const fs::path& modeldir, std::string& err);

/** Poll the catalog until StopSwarmObserver. A failure is logged and does not stop btxd. */
void StartSwarmObserver(const fs::path& modeldir);
void StopSwarmObserver() noexcept;

} // namespace modelnet

#endif // BITCOIN_MODELNET_SWARM_OBSERVER_H
