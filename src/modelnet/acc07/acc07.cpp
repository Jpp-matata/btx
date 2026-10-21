// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc07/acc07.h>
#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {
namespace {

const char* const kKnown[] = {
    "E2E-006",
    "QA-001",
    "QA-002",
    "QA-003",
    "REV-001",
    "REV-002",
    "REV-003",
    "REV-004",
    "REV-005",
    "REV-006",
    "REV-007",
    "REV-008",
    "REV-009",
    "REV-010",
    "REV-011",
    "REV-012",
    "REV-013",
    "REV-014",
    "REV-015",
    "REV-016",
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

bool Known(const std::string& id)
{
    for (const char* row : kKnown) {
        if (id == row) return true;
    }
    return false;
}

bool EngineText(const std::string& text)
{
    for (const char* name : kEngines) {
        if (text == name) return true;
    }
    return false;
}

} // namespace

bool Acc07::stored(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return true;
    }
    return false;
}

std::string Acc07::text(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return row.text;
    }
    return std::string();
}

std::string Acc07::classification() const
{
    return "SIMULATION_NOT_HARDWARE_MEASUREMENT";
}

bool Acc07::Apply(const std::string& id, const std::string& text, int64_t aux, bool flag, std::string& err)
{
    if (id.empty() || !Known(id)) {
        err = "ID";
        return false;
    }
    // REV-016 with non-empty text is decided before the empty-text rule.
    // Empty text stays FIELD. aux and flag still refuse first.
    if (id == "REV-016" && !text.empty()) {
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
        err = NIFDT_UNQUALIFIED;
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
    if (id == "E2E-006" && text == "backup") {
        err = "ROLLBACK";
        return false;
    }
    if (id == "QA-001" && text == "skip") {
        err = "FAIL";
        return false;
    }
    if (id == "QA-002" && text == "native") {
        err = "PACKAGE";
        return false;
    }
    if (id == "QA-003" && text == "growth") {
        err = "CLAIM";
        return false;
    }
    if (id == "REV-001" && text == "1.0.0") {
        err = "REVISION";
        return false;
    }
    if (id == "REV-002" && text == "signed-release") {
        err = "UNSIGNED";
        return false;
    }
    if (id == "REV-004" && text == "cycle") {
        err = "CYCLE";
        return false;
    }
    if (id == "REV-005" && text == "empty-keys") {
        err = "PENDING";
        return false;
    }
    if (id == "REV-008" && text == "early") {
        err = "BATCH";
        return false;
    }
    if (id == "REV-009" && text == "shell") {
        err = "ACTION";
        return false;
    }
    if (id == "REV-010" && text == "default") {
        err = "PROFILE";
        return false;
    }
    if (id == "REV-011" && text == "weaker") {
        err = "STRICT";
        return false;
    }
    if (id == "REV-013" && text == "late") {
        err = "PLAN";
        return false;
    }
    if (id == "REV-014" && text == "shared") {
        err = "TOKEN";
        return false;
    }
    if (id == "REV-015" && text == "score") {
        err = "BINDING";
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

bool Acc07::Hosts(int64_t n, std::string& err)
{
    if (n != static_cast<int64_t>(1)) {
        err = "HOSTS";
        return false;
    }
    err.clear();
    return true;
}

bool Acc07::Release(std::string& err)
{
    err = NIFDT_UNQUALIFIED;
    return false;
}

bool Acc07::Gpu(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc07::Vendor(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc07::Spend(std::string& err)
{
    err = NIFDT_AUTOMATIC_SPEND;
    return false;
}

} // namespace modelnet
