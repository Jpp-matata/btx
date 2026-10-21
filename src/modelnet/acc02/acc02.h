// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_ACC02_ACC02_H
#define BITCOIN_MODELNET_ACC02_ACC02_H

#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {

/**
 * Acc02. Closed records for WIRE-004, ID-001, ID-002, ID-003, ID-004,
 * NET-001, NET-002, NET-003, NET-004, GIT-001, GIT-002, GIT-003, GIT-004,
 * GIT-005, SCM-001, and SCM-002.
 *
 * Apply, first match, stores nothing on failure. An empty or unknown id
 * returns ID. Empty text returns FIELD. aux below 0 returns BOUND. aux
 * above 100 returns CAP. flag false returns ABSENT. Text exactly vllm,
 * sglang, lmcache, nixl, mooncake, rdma, deepinfer, dualpipe, fp8, or fp4
 * returns NIFDT_ENGINE_ABSENT. Text that contains private returns
 * NIFDT_PRIVATE_DATA. WIRE-004 text that contains ? or # returns URI.
 * ID-001 with aux 0 returns EXPIRED. ID-002 text exactly spend returns
 * ROLE. ID-003 text exactly widen returns NARROW. ID-004 text exactly
 * shrink returns THRESHOLD. NET-001 text exactly tofu returns TOFU.
 * NET-002 text exactly downgrade returns DOWNGRADE. NET-003 with aux above
 * 8 returns FANOUT. GIT-002 text exactly sha1 returns DIGEST. GIT-003 text
 * exactly hook returns HOOK. GIT-005 text that does not contain sha256
 * returns OID. SCM-002 text exactly conflict returns CONFLICT. Any other
 * accepted id stores its text once. The same text again returns
 * NIFDT_REPLAY and keeps the first. Different text returns UNCHANGED.
 *
 * Release returns NIFDT_UNQUALIFIED even after every accepted id is stored.
 * release_qualified(), hardware_qualified(), qualified(), advertised(),
 * launched(), gpu(), broadcast(), gpu_passed(), and windows_passed() stay
 * false. hosts() and physical_hosts() stay 1. Hosts with n other than 1
 * returns HOSTS. Spend returns NIFDT_AUTOMATIC_SPEND. bytes_moved() stays
 * 0. Vendor and Gpu return NIFDT_ENGINE_ABSENT. automatic_spend() is
 * NIFDT_AUTOMATIC_SPEND. classification() is
 * SIMULATION_NOT_HARDWARE_MEASUREMENT.
 * This class opens no file, socket, or process.
 */
class Acc02 {
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
