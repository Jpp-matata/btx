// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_ACC07_ACC07_H
#define BITCOIN_MODELNET_ACC07_ACC07_H

#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {

/**
 * Acc07. Closed records for E2E-006, QA-001 through QA-003, and REV-001
 * through REV-016.
 *
 * Apply, first match, stores nothing on failure. An empty or unknown id
 * returns ID. REV-016 is checked before the empty-text rule only when its
 * text is non-empty: aux below 0 is still BOUND, aux above 100 is still CAP,
 * flag false is still ABSENT, and any other non-empty REV-016 text returns
 * NIFDT_UNQUALIFIED. Empty text, including empty REV-016 text, returns FIELD.
 * aux below 0 returns BOUND. aux above 100 returns CAP. flag false returns
 * ABSENT. Exact text vllm, sglang, lmcache, nixl, mooncake, rdma, deepinfer,
 * dualpipe, fp8, or fp4 returns NIFDT_ENGINE_ABSENT. Text containing private
 * returns NIFDT_PRIVATE_DATA.
 * E2E-006 text backup returns ROLLBACK. QA-001 text skip returns FAIL.
 * QA-002 text native returns PACKAGE. QA-003 text growth returns CLAIM.
 * REV-001 text 1.0.0 returns REVISION. REV-002 text signed-release returns
 * UNSIGNED. REV-004 text cycle returns CYCLE. REV-005 text empty-keys returns
 * PENDING. REV-008 text early returns BATCH. REV-009 text shell returns
 * ACTION. REV-010 text default returns PROFILE. REV-011 text weaker returns
 * STRICT. REV-013 text late returns PLAN. REV-014 text shared returns TOKEN.
 * REV-015 text score returns BINDING.
 * Any other accepted call stores that text once. The same text again returns
 * NIFDT_REPLAY and keeps the first. Different text returns UNCHANGED.
 *
 * Release returns NIFDT_UNQUALIFIED after every other id is stored.
 * hosts() and physical_hosts() stay 1. Hosts with n other than 1 returns HOSTS.
 * Spend returns NIFDT_AUTOMATIC_SPEND. bytes_moved() stays 0.
 * qualified(), release_qualified(), hardware_qualified(), launched(), gpu(),
 * gpu_passed(), windows_passed(), broadcast(), and advertised() stay false.
 * classification() is SIMULATION_NOT_HARDWARE_MEASUREMENT.
 * Vendor and Gpu return NIFDT_ENGINE_ABSENT.
 * This class opens no file, socket, or process.
 */
class Acc07 {
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
