// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_ACC03_ACC03_H
#define BITCOIN_MODELNET_ACC03_ACC03_H

#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {

/**
 * Acc03. Closed records for SCM-003, SCM-004, RES-001 through RES-005,
 * ART-001 through ART-003, VER-001 through VER-004, ADM-001, and ADM-002.
 *
 * Apply, first match, stores nothing on failure. An empty or unknown id
 * returns ID. Empty text returns FIELD. aux below 0 returns BOUND. aux
 * above 100 returns CAP. flag false returns ABSENT. Text exactly equal to
 * vllm, sglang, lmcache, nixl, mooncake, rdma, deepinfer, dualpipe, fp8,
 * or fp4 returns NIFDT_ENGINE_ABSENT. Text that contains private returns
 * NIFDT_PRIVATE_DATA. SCM-003 with text fetch-all returns PREFETCH.
 * RES-001 with text assign returns AUTHORITY. RES-003 with text other
 * than LEADING, CONTESTED, NEGLECTED, RECENT, or REPRODUCED returns VIEW.
 * RES-004 with text parser returns PARSER. ART-001 with text tag returns
 * MUTABLE. VER-001 with text network returns FETCH. VER-002 whose text
 * does not contain rng returns STATE. VER-003 with text sample returns
 * GAP. VER-004 with text self returns SELF. ADM-001 with text one-root
 * returns QUORUM. ADM-002 with text other than hostmodel, importmodel,
 * seedmodel, watched, demand-seed, peer-follow, cloud-mirror,
 * encrypted-release, bundle-import, package-install, hosted-gateway,
 * direct-api, capability, local-generate, or model-rollout returns PATH.
 * Any other accepted id stores its text once. The same text again returns
 * NIFDT_REPLAY and keeps the first. Different text returns UNCHANGED.
 *
 * Release returns NIFDT_UNQUALIFIED even after every accepted id is stored.
 * qualified(), release_qualified(), hardware_qualified(), advertised(),
 * launched(), gpu(), gpu_passed(), windows_passed(), and broadcast() stay
 * false. hosts() and physical_hosts() stay 1. Hosts with n other than 1
 * returns HOSTS. Spend returns NIFDT_AUTOMATIC_SPEND. bytes_moved() stays
 * 0. automatic_spend() is NIFDT_AUTOMATIC_SPEND. Vendor and Gpu return
 * NIFDT_ENGINE_ABSENT. classification() is
 * SIMULATION_NOT_HARDWARE_MEASUREMENT.
 * This class opens no file, socket, or process.
 */
class Acc03 {
public:
    bool Apply(const std::string& id, const std::string& text, int64_t aux, bool flag, std::string& err);
    bool stored(const std::string& id) const;
    std::string text(const std::string& id) const;
    std::string classification() const;
    bool Hosts(int64_t n, std::string& err);
    bool Release(std::string& err);
    bool Gpu(std::string& err);
    bool Vendor(std::string& err);
    bool Spend(std::string& err);

    bool launched() const { return false; }
    bool gpu() const { return false; }
    bool gpu_passed() const { return false; }
    bool windows_passed() const { return false; }
    bool broadcast() const { return false; }
    bool qualified() const { return false; }
    bool release_qualified() const { return false; }
    bool hardware_qualified() const { return false; }
    bool advertised() const { return false; }
    int64_t hosts() const { return static_cast<int64_t>(1); }
    int64_t physical_hosts() const { return static_cast<int64_t>(1); }
    int64_t bytes_moved() const { return static_cast<int64_t>(0); }
    std::string automatic_spend() const { return NIFDT_AUTOMATIC_SPEND; }

private:
    struct Row {
        std::string id;
        std::string text;
    };
    std::vector<Row> m_rows;
};

} // namespace modelnet

#endif
