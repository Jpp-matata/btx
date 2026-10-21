// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_ACC01_ACC01_H
#define BITCOIN_MODELNET_ACC01_ACC01_H

#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {

/**
 * Acc01. Closed records for CORE-001 through CORE-005, ARCH-001 through
 * ARCH-004, SEC-001 through SEC-004, and WIRE-001 through WIRE-003.
 *
 * Apply, first match, stores nothing on failure. An empty or unknown id
 * returns ID. Empty text returns FIELD. aux below 0 returns BOUND. aux
 * above 100 returns CAP. flag false returns ABSENT. Text exactly vllm,
 * sglang, lmcache, nixl, mooncake, rdma, deepinfer, dualpipe, fp8, or fp4
 * returns NIFDT_ENGINE_ABSENT. ARCH-004 with text exactly private_personal
 * is stored; that check is before the private substring check. Text that
 * contains private returns NIFDT_PRIVATE_DATA. CORE-003 text that contains
 * consensus returns CONSENSUS. CORE-005 text that contains claim returns
 * CLAIM. ARCH-001 with text exactly hardware returns NIFDT_UNQUALIFIED.
 * ARCH-002 with text exactly assign returns AUTHORITY. ARCH-004 text that
 * is not exactly public_participant, private_personal, community_realm,
 * enterprise_ingress, enterprise_site, or offline_site returns PROFILE.
 * SEC-002 with text exactly shell returns QUARANTINE. WIRE-001 text that
 * does not contain 1.1.0 returns VERSION. WIRE-003 with text exactly
 * oversize returns LIMIT. Any other accepted id stores its text once.
 * The same text again returns NIFDT_REPLAY and keeps the first. Different
 * text returns UNCHANGED.
 *
 * Release returns NIFDT_UNQUALIFIED. release_qualified(),
 * hardware_qualified(), qualified(), advertised(), launched(), gpu(),
 * broadcast(), gpu_passed(), and windows_passed() stay false.
 * hosts() and physical_hosts() stay 1. Hosts with n other than 1 returns
 * HOSTS. Spend returns NIFDT_AUTOMATIC_SPEND. bytes_moved() stays 0.
 * Vendor and Gpu return NIFDT_ENGINE_ABSENT.
 * classification() is SIMULATION_NOT_HARDWARE_MEASUREMENT.
 * This class opens no file, socket, or process.
 */
class Acc01 {
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
    bool broadcast() const { return false; }
    bool qualified() const { return false; }
    bool release_qualified() const { return false; }
    bool hardware_qualified() const { return false; }
    bool advertised() const { return false; }
    bool gpu_passed() const { return false; }
    bool windows_passed() const { return false; }
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
