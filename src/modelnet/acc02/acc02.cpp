// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc02/acc02.h>
#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {
namespace {

const char* const kKnown[] = {
    "WIRE-004",
    "ID-001",
    "ID-002",
    "ID-003",
    "ID-004",
    "NET-001",
    "NET-002",
    "NET-003",
    "NET-004",
    "GIT-001",
    "GIT-002",
    "GIT-003",
    "GIT-004",
    "GIT-005",
    "SCM-001",
    "SCM-002",
};

bool Known(const std::string& id)
{
    for (const char* row : kKnown) {
        if (id == row) return true;
    }
    return false;
}

bool ExactEngine(const std::string& text)
{
    return text == "vllm" || text == "sglang" || text == "lmcache" ||
           text == "nixl" || text == "mooncake" || text == "rdma" ||
           text == "deepinfer" || text == "dualpipe" || text == "fp8" ||
           text == "fp4";
}

} // namespace

bool Acc02::stored(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return true;
    }
    return false;
}

std::string Acc02::text(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return row.text;
    }
    return std::string();
}

std::string Acc02::classification() const
{
    return "SIMULATION_NOT_HARDWARE_MEASUREMENT";
}

bool Acc02::Apply(const std::string& id, const std::string& text, int64_t aux, bool flag, std::string& err)
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
    if (ExactEngine(text)) {
        err = NIFDT_ENGINE_ABSENT;
        return false;
    }
    if (text.find("private") != std::string::npos) {
        err = NIFDT_PRIVATE_DATA;
        return false;
    }
    if (id == "WIRE-004" &&
        (text.find('?') != std::string::npos || text.find('#') != std::string::npos)) {
        err = "URI";
        return false;
    }
    if (id == "ID-001" && aux == static_cast<int64_t>(0)) {
        err = "EXPIRED";
        return false;
    }
    if (id == "ID-002" && text == "spend") {
        err = "ROLE";
        return false;
    }
    if (id == "ID-003" && text == "widen") {
        err = "NARROW";
        return false;
    }
    if (id == "ID-004" && text == "shrink") {
        err = "THRESHOLD";
        return false;
    }
    if (id == "NET-001" && text == "tofu") {
        err = "TOFU";
        return false;
    }
    if (id == "NET-002" && text == "downgrade") {
        err = "DOWNGRADE";
        return false;
    }
    if (id == "NET-003" && aux > static_cast<int64_t>(8)) {
        err = "FANOUT";
        return false;
    }
    if (id == "GIT-002" && text == "sha1") {
        err = "DIGEST";
        return false;
    }
    if (id == "GIT-003" && text == "hook") {
        err = "HOOK";
        return false;
    }
    if (id == "GIT-005" && text.find("sha256") == std::string::npos) {
        err = "OID";
        return false;
    }
    if (id == "SCM-002" && text == "conflict") {
        err = "CONFLICT";
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

bool Acc02::Hosts(int64_t n, std::string& err)
{
    if (n != static_cast<int64_t>(1)) {
        err = "HOSTS";
        return false;
    }
    err.clear();
    return true;
}

bool Acc02::Release(std::string& err)
{
    err = NIFDT_UNQUALIFIED;
    return false;
}

bool Acc02::Gpu(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc02::Vendor(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc02::Spend(std::string& err)
{
    err = NIFDT_AUTOMATIC_SPEND;
    return false;
}

} // namespace modelnet
