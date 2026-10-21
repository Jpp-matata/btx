// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/identity_chain/identity_chain.h>

#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>

namespace modelnet {

IdentityChain::Record* IdentityChain::Find(const std::string& id)
{
    for (Record& row : m_rows) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

const IdentityChain::Record* IdentityChain::Find(const std::string& id) const
{
    for (const Record& row : m_rows) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

bool IdentityChain::stored(const std::string& id) const
{
    return Find(id) != nullptr;
}

int64_t IdentityChain::epoch(const std::string& id) const
{
    const Record* row = Find(id);
    if (row == nullptr) return static_cast<int64_t>(0);
    return row->epoch;
}

bool IdentityChain::Open(const std::string& id, std::string& err)
{
    if (id.empty()) {
        err = IDENTITY_CHAIN_FIELD;
        return false;
    }
    if (Find(id) != nullptr) {
        err = NIFDT_REPLAY;
        return false;
    }
    Record row;
    row.id = id;
    row.epoch = static_cast<int64_t>(1);
    m_rows.push_back(row);
    err.clear();
    return true;
}

bool IdentityChain::Rotate(const std::string& id, int64_t now_ms, int64_t until_ms, std::string& err)
{
    Record* row = Find(id);
    if (row == nullptr) {
        err = IDENTITY_CHAIN_IDENTITY;
        return false;
    }
    // Equality is expired. The stored epoch stays where it is.
    if (until_ms <= now_ms) {
        err = IDENTITY_CHAIN_EXPIRED;
        return false;
    }
    row->epoch += static_cast<int64_t>(1);
    err.clear();
    return true;
}

bool IdentityChain::Recover(const std::string& id, int64_t epoch, std::string& err)
{
    Record* row = Find(id);
    if (row == nullptr) {
        err = IDENTITY_CHAIN_IDENTITY;
        return false;
    }
    if (epoch < row->epoch) {
        err = IDENTITY_CHAIN_STALE;
        return false;
    }
    if (epoch != row->epoch) {
        err = IDENTITY_CHAIN_EPOCH;
        return false;
    }
    err.clear();
    return true;
}

bool IdentityChain::Spend(std::string& err)
{
    err = NIFDT_AUTOMATIC_SPEND;
    return false;
}

bool IdentityChain::Release(std::string& err)
{
    err = NIFDT_UNQUALIFIED;
    return false;
}

bool IdentityChain::Vendor(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool IdentityChain::Gpu(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

} // namespace modelnet
