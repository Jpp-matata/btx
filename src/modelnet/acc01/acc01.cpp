// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc01/acc01.h>
#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {
namespace {

const char* const kKnown[] = {
    "CORE-001",
    "CORE-002",
    "CORE-003",
    "CORE-004",
    "CORE-005",
    "ARCH-001",
    "ARCH-002",
    "ARCH-003",
    "ARCH-004",
    "SEC-001",
    "SEC-002",
    "SEC-003",
    "SEC-004",
    "WIRE-001",
    "WIRE-002",
    "WIRE-003",
};

bool Known(const std::string& id)
{
    for (const char* row : kKnown) {
        if (id == row) return true;
    }
    return false;
}

bool ExactAbsentEngine(const std::string& text)
{
    return text == "vllm" || text == "sglang" || text == "lmcache" ||
           text == "nixl" || text == "mooncake" || text == "rdma" ||
           text == "deepinfer" || text == "dualpipe" || text == "fp8" ||
           text == "fp4";
}

bool Arch004Profile(const std::string& text)
{
    return text == "public_participant" || text == "private_personal" ||
           text == "community_realm" || text == "enterprise_ingress" ||
           text == "enterprise_site" || text == "offline_site";
}

} // namespace

bool Acc01::stored(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return true;
    }
    return false;
}

std::string Acc01::text(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return row.text;
    }
    return std::string();
}

std::string Acc01::classification() const
{
    return "SIMULATION_NOT_HARDWARE_MEASUREMENT";
}

bool Acc01::Apply(const std::string& id, const std::string& text, int64_t aux, bool flag, std::string& err)
{
    if (id.empty() || !Known(id)) {
        err = "ID";
        return false;
    }
    if (text.empty()) {
        err = "FIELD";
        return false;
    }
    if (aux < static_cast<int64_t>(0)) {
        err = "BOUND";
        return false;
    }
    if (aux > static_cast<int64_t>(100)) {
        err = "CAP";
        return false;
    }
    if (!flag) {
        err = "ABSENT";
        return false;
    }
    if (ExactAbsentEngine(text)) {
        err = NIFDT_ENGINE_ABSENT;
        return false;
    }
    // private_personal is a listed ARCH-004 profile. Accept it before the
    // private substring refusal so that exact text can be stored.
    if (!(id == "ARCH-004" && text == "private_personal") &&
        text.find("private") != std::string::npos) {
        err = NIFDT_PRIVATE_DATA;
        return false;
    }
    if (id == "CORE-003" && text.find("consensus") != std::string::npos) {
        err = "CONSENSUS";
        return false;
    }
    if (id == "CORE-005" && text.find("claim") != std::string::npos) {
        err = "CLAIM";
        return false;
    }
    if (id == "ARCH-001" && text == "hardware") {
        err = NIFDT_UNQUALIFIED;
        return false;
    }
    if (id == "ARCH-002" && text == "assign") {
        err = "AUTHORITY";
        return false;
    }
    if (id == "ARCH-004" && !Arch004Profile(text)) {
        err = "PROFILE";
        return false;
    }
    if (id == "SEC-002" && text == "shell") {
        err = "QUARANTINE";
        return false;
    }
    if (id == "WIRE-001" && text.find("1.1.0") == std::string::npos) {
        err = "VERSION";
        return false;
    }
    if (id == "WIRE-003" && text == "oversize") {
        err = "LIMIT";
        return false;
    }
    for (const Row& row : m_rows) {
        if (row.id != id) continue;
        if (row.text == text) {
            err = NIFDT_REPLAY;
            return false;
        }
        err = "UNCHANGED";
        return false;
    }
    m_rows.push_back(Row{id, text});
    err.clear();
    return true;
}

bool Acc01::Hosts(int64_t n, std::string& err)
{
    if (n != static_cast<int64_t>(1)) {
        err = "HOSTS";
        return false;
    }
    err.clear();
    return true;
}

bool Acc01::Release(std::string& err)
{
    err = NIFDT_UNQUALIFIED;
    return false;
}

bool Acc01::Gpu(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc01::Vendor(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc01::Spend(std::string& err)
{
    err = NIFDT_AUTOMATIC_SPEND;
    return false;
}

} // namespace modelnet
