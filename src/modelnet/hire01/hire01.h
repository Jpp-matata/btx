// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_HIRE01_HIRE01_H
#define BITCOIN_MODELNET_HIRE01_HIRE01_H

#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>

namespace modelnet {

/**
 * Hire01. Controls on a modified model can be recorded. A release is not
 * issued. This class opens no socket, file, or process.
 *
 * QualifyModified: an empty model_id is FIELD and stores nothing. If
 * external_controls or outcome_tested is false, the result is
 * NIFDT_UNQUALIFIED and nothing is stored. When both are true the model_id
 * is stored. That records the controls. release_qualified() stays false.
 *
 * AdvertiseWarm: an empty pool or slots < 0 is FIELD. measured false is
 * CAPACITY and advertised_slots() stays 0. measured true with slots >= 0
 * stores slots. advertised_slots() is 0 unless AdvertiseWarm succeeded.
 *
 * IssueRelease: an empty version is FIELD. gates_passed false and
 * gates_passed true are both NIFDT_UNQUALIFIED. release_issued() stays
 * false. This module does not issue a release.
 *
 * passed(), gpu_passed(), windows_passed(), hardware_qualified(), and
 * release_qualified() stay false. automatic_spend() is
 * NIFDT_AUTOMATIC_SPEND.
 */
class Hire01 {
public:
    bool QualifyModified(const std::string& model_id, bool external_controls, bool outcome_tested, std::string& err);
    bool AdvertiseWarm(const std::string& pool, int64_t slots, bool measured, std::string& err);
    bool IssueRelease(const std::string& version, bool gates_passed, std::string& err);

    std::string model_id() const { return m_model_id; }
    int64_t advertised_slots() const;
    bool release_issued() const { return false; }

    bool passed() const { return false; }
    bool gpu_passed() const { return false; }
    bool windows_passed() const { return false; }
    bool hardware_qualified() const { return false; }
    bool release_qualified() const { return false; }
    std::string automatic_spend() const { return NIFDT_AUTOMATIC_SPEND; }

private:
    std::string m_model_id;
    int64_t m_slots{static_cast<int64_t>(0)};
    bool m_advertised{false};
};

} // namespace modelnet

#endif // BITCOIN_MODELNET_HIRE01_HIRE01_H
