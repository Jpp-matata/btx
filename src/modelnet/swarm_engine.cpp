// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_engine.h>

#include <cmath>

namespace modelnet {

SwarmEngineStatus SwarmProbeEngine(const std::string& name, const fs::path& binary, std::string& err)
{
    SwarmEngineStatus status;
    status.name = name;
    if (name.empty() || binary.empty()) {
        status.code = "ENGINE";
        err = "ENGINE";
        return status;
    }
    if (!fs::exists(binary) || !fs::is_regular_file(binary)) {
        status.code = "ENGINE_ABSENT";
        err = "ENGINE_ABSENT";
        return status;
    }
    status.present = true;
    status.code = "INSTALLED";
    err.clear();
    return status;
}

bool SwarmQualifyEngine(SwarmEngineStatus& status, bool measurement_supplied, double tokens_per_sec, std::string& err)
{
    status.qualified = false;
    status.advertised = false;
    if (!status.present || status.code != "INSTALLED") {
        status.code = "ENGINE_ABSENT";
        err = "ENGINE_ABSENT";
        return false;
    }
    if (!measurement_supplied || !std::isfinite(tokens_per_sec) || tokens_per_sec <= 0.0) {
        err = "UNQUALIFIED";
        return false;
    }
    status.qualified = true;
    status.advertised = true;
    status.code = "QUALIFIED";
    err.clear();
    return true;
}

} // namespace modelnet
