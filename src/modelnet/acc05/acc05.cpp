// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc05/acc05.h>
#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {
namespace {

const char* const kKnown[] = {
    "WAN-002",
    "WAN-003",
    "WAN-004",
    "WAN-005",
    "ENT-001",
    "ENT-002",
    "ENT-003",
    "ECON-001",
    "ECON-002",
    "STORE-001",
    "STORE-002",
    "STORE-003",
    "API-001",
    "API-002",
    "API-003",
    "API-004",
};

const char* const kEngines[] = {
    "vllm",
    "sglang",
    "lmcache",
    "nixl",
    "mooncake",
    "rdma",
    "deepinfer",
    "dualpipe",
    "fp8",
    "fp4",
};

const char* const kWan002[] = {
    "discovered",
    "fetching",
    "staged",
    "verified",
    "canary",
    "active",
    "paused",
    "failed",
    "rolled_back",
    "unknown",
};

const char* const kApi003[] = {
    "identity",
    "git",
    "workspace",
    "research",
    "evidence",
    "model",
    "agent",
    "realm",
    "promotion",
    "channel",
    "rollout",
    "bundle",
    "migration",
};

bool Known(const std::string& id)
{
    for (const char* row : kKnown) {
        if (id == row) return true;
    }
    return false;
}

bool Exact(const std::string& text, const char* const* rows, int n)
{
    for (int i = 0; i < n; ++i) {
        if (text == rows[i]) return true;
    }
    return false;
}

bool EngineText(const std::string& text)
{
    return Exact(text, kEngines, static_cast<int>(sizeof(kEngines) / sizeof(kEngines[0])));
}

bool Wan002State(const std::string& text)
{
    return Exact(text, kWan002, static_cast<int>(sizeof(kWan002) / sizeof(kWan002[0])));
}

bool Api003Name(const std::string& text)
{
    return Exact(text, kApi003, static_cast<int>(sizeof(kApi003) / sizeof(kApi003[0])));
}

} // namespace

bool Acc05::stored(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return true;
    }
    return false;
}

std::string Acc05::text(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return row.text;
    }
    return std::string();
}

std::string Acc05::classification() const
{
    return "SIMULATION_NOT_HARDWARE_MEASUREMENT";
}

bool Acc05::Apply(const std::string& id, const std::string& text, int64_t aux, bool flag, std::string& err)
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
    if (EngineText(text)) {
        err = NIFDT_ENGINE_ABSENT;
        return false;
    }
    if (text.find("private") != std::string::npos) {
        err = NIFDT_PRIVATE_DATA;
        return false;
    }
    if (id == "WAN-002" && !Wan002State(text)) {
        err = "STATE";
        return false;
    }
    if (id == "WAN-003" && text == "mixed") {
        err = "GENERATION";
        return false;
    }
    if (id == "WAN-005" && text == "symlink") {
        err = "TAR";
        return false;
    }
    if (id == "ENT-001" && text == "rpc") {
        err = "ROUTE";
        return false;
    }
    if (id == "ENT-003" && text == "endorse") {
        err = "REVIEW";
        return false;
    }
    if (id == "ECON-001" && text == "spend") {
        err = "WALLET";
        return false;
    }
    if (id == "STORE-002" && text == "retry") {
        err = "UNKNOWN";
        return false;
    }
    if (id == "STORE-003" && text == "skip-sig") {
        err = "LIMIT";
        return false;
    }
    if (id == "API-001" && text.find("idempotency") == std::string::npos) {
        err = "IDEMPOTENT";
        return false;
    }
    if (id == "API-003" && !Api003Name(text)) {
        err = "CLI";
        return false;
    }
    if (id == "API-004" && text == "trusted") {
        err = "BADGE";
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

bool Acc05::Hosts(int64_t n, std::string& err)
{
    if (n != static_cast<int64_t>(1)) {
        err = "HOSTS";
        return false;
    }
    err.clear();
    return true;
}

bool Acc05::Release(std::string& err)
{
    err = NIFDT_UNQUALIFIED;
    return false;
}

bool Acc05::Gpu(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc05::Vendor(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc05::Spend(std::string& err)
{
    err = NIFDT_AUTOMATIC_SPEND;
    return false;
}

} // namespace modelnet
