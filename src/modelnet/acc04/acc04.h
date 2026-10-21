// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_ACC04_ACC04_H
#define BITCOIN_MODELNET_ACC04_ACC04_H

#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {

/**
 * Acc04. Closed records for ADM-003, RUN-001 through RUN-004, RSI-001
 * through RSI-003, REALM-001 through REALM-003, MDT-001 through MDT-004,
 * and WAN-001.
 *
 * Apply checks, in order, and stores nothing on failure. An empty or
 * unknown id returns ID. Empty text returns FIELD. aux below 0 returns
 * BOUND. aux above 100 returns CAP. flag false returns ABSENT. Exact text
 * vllm, sglang, lmcache, nixl, mooncake, rdma, deepinfer, dualpipe, fp8, or
 * fp4 returns NIFDT_ENGINE_ABSENT. Text that contains private returns
 * NIFDT_PRIVATE_DATA. ADM-003 with text kill returns ADVISORY. RUN-002 with
 * text passthrough returns SCOPE. RUN-003 with text recall returns SENT.
 * RSI-001 with text inherit returns GRANT. RSI-002 with text skip returns
 * REGRESSION. RSI-003 with text transfer returns GENERATION. REALM-002 with
 * text bridge returns BRIDGE. REALM-003 with text transitive returns TRUST.
 * MDT-001 with text monetary returns STORE. MDT-002 with text latest returns
 * DISAGREE. MDT-004 with text rewrite returns FOREIGN. WAN-001 with text
 * older returns ROLLBACK. Any other accepted id stores its text once. The
 * same text again returns NIFDT_REPLAY and keeps the first. Different text
 * returns UNCHANGED. classification() is SIMULATION_NOT_HARDWARE_MEASUREMENT.
 *
 * Release returns NIFDT_UNQUALIFIED. Spend returns NIFDT_AUTOMATIC_SPEND.
 * qualified(), release_qualified(), hardware_qualified(), advertised(),
 * launched(), gpu(), gpu_passed(), windows_passed(), and broadcast() stay
 * false, including after RSI-003 is refused. hosts() and physical_hosts()
 * stay 1. Hosts with n other than 1 returns HOSTS. bytes_moved() stays 0.
 * Vendor and Gpu return NIFDT_ENGINE_ABSENT.
 * This class opens no file, socket, or process.
 */
class Acc04 {
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
