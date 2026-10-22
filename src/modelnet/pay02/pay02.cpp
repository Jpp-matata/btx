// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/pay02/pay02.h>

#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>

namespace modelnet {

bool Pay02::Quiet(std::string& err) const
{
    if (m_pay.automatic_spend() != NIFDT_AUTOMATIC_SPEND || m_pay.atoms_moved() != static_cast<int64_t>(0) ||
        automatic_spend() != NIFDT_AUTOMATIC_SPEND || atoms_moved() != static_cast<int64_t>(0) || settled() ||
        m_pay.broadcast() || m_pay.spendable() || m_pay.qualified() || m_pay.mainnet()) {
        err = NIFDT_AUTOMATIC_SPEND;
        return false;
    }
    return true;
}

bool Pay02::RecordFunding(const std::string& objective, bool refund_path, bool payout_path, std::string& err)
{
    if (objective.empty()) {
        err = "FIELD";
        return false;
    }
    if (!refund_path || !payout_path) {
        err = "PATH";
        return false;
    }
    if (!m_opened) {
        if (!m_pay.Open("record", "refund", "payout", err)) return false;
        if (!Quiet(err)) return false;
        m_opened = true;
    }
    if (!m_pay.Claim(objective, err)) return false;
    if (!Quiet(err)) return false;
    m_objective = objective;
    err.clear();
    return true;
}

bool Pay02::ProveExit(bool unilateral, bool expiry, bool fee, bool stale, bool watchtower, std::string& err)
{
    if (!unilateral || !expiry || !fee || !stale || !watchtower) {
        err = NIFDT_UNQUALIFIED;
        return false;
    }
    err = NIFDT_UNQUALIFIED;
    return false;
}

bool Pay02::Federate(int organizations, bool local_validation, std::string& err)
{
    if (organizations < 2 || !local_validation) {
        err = "ORG";
        return false;
    }
    err = NIFDT_UNQUALIFIED;
    return false;
}

bool Pay02::ExerciseFaults(bool partition, bool clock_fault, bool response_failure, bool liquidity_loss, int hosts,
                           std::string& err)
{
    if (hosts < 2) {
        err = "HOSTS";
        return false;
    }
    if (!partition || !clock_fault || !response_failure || !liquidity_loss) {
        err = "FAULT";
        return false;
    }
    if (this->hosts() != static_cast<int64_t>(1)) {
        err = "HOSTS";
        return false;
    }
    err = NIFDT_UNQUALIFIED;
    return false;
}

} // namespace modelnet
