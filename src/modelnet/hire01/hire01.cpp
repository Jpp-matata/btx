// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/hire01/hire01.h>

#include <cstdint>
#include <string>

namespace modelnet {

bool Hire01::QualifyModified(const std::string& model_id, bool external_controls, bool outcome_tested, std::string& err)
{
    if (model_id.empty()) {
        err = "FIELD";
        return false;
    }
    // Controls are recorded only when both checks are present.
    if (!external_controls || !outcome_tested) {
        err = NIFDT_UNQUALIFIED;
        return false;
    }
    m_model_id = model_id;
    err.clear();
    return true;
}

bool Hire01::AdvertiseWarm(const std::string& pool, int64_t slots, bool measured, std::string& err)
{
    if (pool.empty() || slots < static_cast<int64_t>(0)) {
        err = "FIELD";
        return false;
    }
    // Unmeasured capacity is not an advertisement.
    if (!measured) {
        err = "CAPACITY";
        return false;
    }
    m_slots = slots;
    m_advertised = true;
    err.clear();
    return true;
}

bool Hire01::IssueRelease(const std::string& version, bool gates_passed, std::string& err)
{
    if (version.empty()) {
        err = "FIELD";
        return false;
    }
    // This module does not issue a release, with or without gates.
    if (!gates_passed) {
        err = NIFDT_UNQUALIFIED;
        return false;
    }
    err = NIFDT_UNQUALIFIED;
    return false;
}

int64_t Hire01::advertised_slots() const
{
    if (!m_advertised) return static_cast<int64_t>(0);
    return m_slots;
}

} // namespace modelnet
