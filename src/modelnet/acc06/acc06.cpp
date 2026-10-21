// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc06/acc06.h>
#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {
namespace {

const char* const kKnown[] = {
    "MIG-001",
    "MIG-002",
    "MIG-003",
    "BUILD-001",
    "BUILD-002",
    "BUILD-003",
    "INC-001",
    "INC-002",
    "DEV-001",
    "DEV-002",
    "DEV-003",
    "E2E-001",
    "E2E-002",
    "E2E-003",
    "E2E-004",
    "E2E-005",
};

bool Known(const std::string& id)
{
    for (const char* row : kKnown) {
        if (id == row) return true;
    }
    return false;
}

bool EngineExact(const std::string& text)
{
    return text == "vllm" || text == "sglang" || text == "lmcache" ||
           text == "nixl" || text == "mooncake" || text == "rdma" ||
           text == "deepinfer" || text == "dualpipe" || text == "fp8" ||
           text == "fp4";
}

} // namespace

bool Acc06::stored(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return true;
    }
    return false;
}

std::string Acc06::text(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return row.text;
    }
    return std::string();
}

std::string Acc06::classification() const
{
    return "SIMULATION_NOT_HARDWARE_MEASUREMENT";
}

bool Acc06::Apply(const std::string& id, const std::string& text, int64_t aux, bool flag, std::string& err)
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
    if (EngineExact(text)) {
        err = NIFDT_ENGINE_ABSENT;
        return false;
    }
    if (text.find("private") != std::string::npos) {
        err = NIFDT_PRIVATE_DATA;
        return false;
    }
    if (id == "MIG-001" && text == "wallet") {
        err = "UNTOUCHED";
        return false;
    }
    if (id == "MIG-002" && text == "fabricate") {
        err = "LEGACY";
        return false;
    }
    if (id == "MIG-003" && text == "downgrade") {
        err = "PROTOCOL";
        return false;
    }
    if (id == "BUILD-002" && (text == "windows" || text == "macos")) {
        err = NIFDT_UNQUALIFIED;
        return false;
    }
    if (id == "BUILD-003" && text == "skip") {
        err = "MEASURE";
        return false;
    }
    if (id == "INC-001" && text == "blocklist") {
        err = "SCOPE";
        return false;
    }
    if (id == "INC-002" && text == "self") {
        err = "RUNBOOK";
        return false;
    }
    if (id == "DEV-002" && text == "grep") {
        err = "EVIDENCE";
        return false;
    }
    if (id == "DEV-003" && text == "doc") {
        err = "JOURNEY";
        return false;
    }
    if (id == "E2E-001" && text == "assign") {
        err = "AUTHORITY";
        return false;
    }
    if (id == "E2E-002" && text == "altered") {
        err = "MISMATCH";
        return false;
    }
    if (id == "E2E-003" && text == "hook") {
        err = "SAFE";
        return false;
    }
    if (id == "E2E-004" && text == "one-attestor") {
        err = "QUORUM";
        return false;
    }
    if (id == "E2E-005" && text == "shrink") {
        err = "THRESHOLD";
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

bool Acc06::Hosts(int64_t n, std::string& err)
{
    if (n != static_cast<int64_t>(1)) {
        err = "HOSTS";
        return false;
    }
    err.clear();
    return true;
}

bool Acc06::Release(std::string& err)
{
    err = NIFDT_UNQUALIFIED;
    return false;
}

bool Acc06::Gpu(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc06::Vendor(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc06::Spend(std::string& err)
{
    err = NIFDT_AUTOMATIC_SPEND;
    return false;
}

} // namespace modelnet
