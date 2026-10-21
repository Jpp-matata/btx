// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc03/acc03.h>
#include <modelnet/nifdt_contract/contract.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {
namespace {

const char* const kKnown[] = {
    "SCM-003",
    "SCM-004",
    "RES-001",
    "RES-002",
    "RES-003",
    "RES-004",
    "RES-005",
    "ART-001",
    "ART-002",
    "ART-003",
    "VER-001",
    "VER-002",
    "VER-003",
    "VER-004",
    "ADM-001",
    "ADM-002",
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

const char* const kViews[] = {
    "LEADING",
    "CONTESTED",
    "NEGLECTED",
    "RECENT",
    "REPRODUCED",
};

const char* const kPaths[] = {
    "hostmodel",
    "importmodel",
    "seedmodel",
    "watched",
    "demand-seed",
    "peer-follow",
    "cloud-mirror",
    "encrypted-release",
    "bundle-import",
    "package-install",
    "hosted-gateway",
    "direct-api",
    "capability",
    "local-generate",
    "model-rollout",
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
    for (const char* name : kEngines) {
        if (text == name) return true;
    }
    return false;
}

bool ViewName(const std::string& text)
{
    for (const char* name : kViews) {
        if (text == name) return true;
    }
    return false;
}

bool PathName(const std::string& text)
{
    for (const char* name : kPaths) {
        if (text == name) return true;
    }
    return false;
}

} // namespace

bool Acc03::stored(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return true;
    }
    return false;
}

std::string Acc03::text(const std::string& id) const
{
    for (const Row& row : m_rows) {
        if (row.id == id) return row.text;
    }
    return std::string();
}

std::string Acc03::classification() const
{
    return "SIMULATION_NOT_HARDWARE_MEASUREMENT";
}

bool Acc03::Apply(const std::string& id, const std::string& text, int64_t aux, bool flag, std::string& err)
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
    if (id == "SCM-003" && text == "fetch-all") {
        err = "PREFETCH";
        return false;
    }
    if (id == "RES-001" && text == "assign") {
        err = "AUTHORITY";
        return false;
    }
    if (id == "RES-003" && !ViewName(text)) {
        err = "VIEW";
        return false;
    }
    if (id == "RES-004" && text == "parser") {
        err = "PARSER";
        return false;
    }
    if (id == "ART-001" && text == "tag") {
        err = "MUTABLE";
        return false;
    }
    if (id == "VER-001" && text == "network") {
        err = "FETCH";
        return false;
    }
    if (id == "VER-002" && text.find("rng") == std::string::npos) {
        err = "STATE";
        return false;
    }
    if (id == "VER-003" && text == "sample") {
        err = "GAP";
        return false;
    }
    if (id == "VER-004" && text == "self") {
        err = "SELF";
        return false;
    }
    if (id == "ADM-001" && text == "one-root") {
        err = "QUORUM";
        return false;
    }
    if (id == "ADM-002" && !PathName(text)) {
        err = "PATH";
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

bool Acc03::Hosts(int64_t n, std::string& err)
{
    if (n != static_cast<int64_t>(1)) {
        err = "HOSTS";
        return false;
    }
    err.clear();
    return true;
}

bool Acc03::Release(std::string& err)
{
    err = NIFDT_UNQUALIFIED;
    return false;
}

bool Acc03::Gpu(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc03::Vendor(std::string& err)
{
    err = NIFDT_ENGINE_ABSENT;
    return false;
}

bool Acc03::Spend(std::string& err)
{
    err = NIFDT_AUTOMATIC_SPEND;
    return false;
}

} // namespace modelnet
