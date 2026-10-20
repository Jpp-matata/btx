// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_graph.h>

namespace modelnet {

SwarmWorkHint SwarmPlanWorkGraph(bool tool_running, bool child_known, bool branch_inactive, bool shared_ancestor,
                                 bool engine_present, std::string& err)
{
    SwarmWorkHint hint;
    hint.retain_parent = tool_running;
    hint.demote_inactive = branch_inactive;
    hint.prefer_ancestor = shared_ancestor;
    hint.bytes_moved = false;
    hint.prefetch_child = false;
    if (child_known) {
        hint.prefetch_child = true;
        hint.code = engine_present ? "LOCAL" : "ENGINE_ABSENT";
    }
    err.clear();
    return hint;
}

} // namespace modelnet
