// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc04/acc04.h>
#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {
namespace {

const char* const kKnown[] = {
    "ADM-003",
    "RUN-001",
    "RUN-002",
    "RUN-003",
    "RUN-004",
    "RSI-001",
    "RSI-002",
    "RSI-003",
    "REALM-001",
    "REALM-002",
    "REALM-003",
    "MDT-001",
    "MDT-002",
    "MDT-003",
    "MDT-004",
    "WAN-001",
};

const char* const kEngine[] = {
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

bool Known(const std::string& id)
{
    for (const char* row : kKnown) {
        if (id == row) return true;
    }
    return false;
}

bool EngineText(const std::string& text)
{
    for (const char* name : kEngine) {
        if (text == name) return true;
    }
    return false;
}

} // namespace

bool Acc04::stored(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return true;
    }
    return false;
}

std::string Acc04::text(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return row.text;
    }
    return std::string();
}

std::string Acc04::classification() const
{
    return "SIMULATION_NOT_HARDWARE_MEASUREMENT";
}

bool Acc04::Apply(const std::string& id, const std::string& text, int64_t aux, bool flag, std::string& err)
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
    if (id == "ADM-003" && text == "kill") {
        err = "ADVISORY";
        return false;
    }
    if (id == "RUN-002" && text == "passthrough") {
        err = "SCOPE";
        return false;
    }
    if (id == "RUN-003" && text == "recall") {
        err = "SENT";
        return false;
    }
    if (id == "RSI-001" && text == "inherit") {
        err = "GRANT";
        return false;
    }
    if (id == "RSI-002" && text == "skip") {
        err = "REGRESSION";
        return false;
    }
    if (id == "RSI-003" && text == "transfer") {
        err = "GENERATION";
        return false;
    }
    if (id == "REALM-002" && text == "bridge") {
        err = "BRIDGE";
        return false;
    }
    if (id == "REALM-003" && text == "transitive") {
        err = "TRUST";
        return false;
    }
    if (id == "MDT-001" && text == "monetary") {
        err = "STORE";
        return false;
    }
    if (id == "MDT-002" && text == "latest") {
        err = "DISAGREE";
        return false;
    }
    if (id == "MDT-004" && text == "rewrite") {
        err = "FOREIGN";
        return false;
    }
    if (id == "WAN-001" && text == "older") {
        err = "ROLLBACK";
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

bool Acc04::Hosts(int64_t n, std::string& err)
{
    if (n != static_cast<int64_t>(1)) {
        err = "HOSTS";
        return false;
    }
    err.clear();
    return true;
}

bool Acc04::Release(std::string& err)
{
    err = NIFDT_UNQUALIFIED;
    return false;
}

bool Acc04::Gpu(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc04::Vendor(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc04::Spend(std::string& err)
{
    err = NIFDT_AUTOMATIC_SPEND;
    return false;
}

} // namespace modelnet
