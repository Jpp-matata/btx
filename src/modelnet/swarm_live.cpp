// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_live.h>

#include <modelnet/swarm_records.h>
#include <univalue.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace modelnet {
namespace {

bool ReadRaw(const fs::path& path, std::string& raw)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool EnsureDir(const fs::path& modeldir, fs::path& swarm_dir, std::string& err, const char* code)
{
    if (modeldir.empty()) {
        err = code;
        return false;
    }
    swarm_dir = modeldir / "swarm";
    fs::create_directories(swarm_dir);
    if (!fs::exists(swarm_dir)) {
        err = code;
        return false;
    }
    return true;
}

bool WriteAtomic(const fs::path& final_path, const std::string& body, std::string& err, const char* write_code,
                 const char* rename_code)
{
    const fs::path tmp = fs::u8path(final_path.utf8string() + ".tmp");
    {
        std::ofstream file(tmp, std::ios::trunc);
        if (!file) {
            err = write_code;
            return false;
        }
        file << body;
        if (!file) {
            err = write_code;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, final_path, ec);
    if (ec) {
        err = rename_code;
        return false;
    }
    err.clear();
    return true;
}

bool FourBit(const std::string& quantization)
{
    return quantization == "4bit" || quantization == "4-bit";
}

bool PrivateKnowledge(const std::string& kind)
{
    return kind == "prompt" || kind == "repository" || kind == "credential";
}

bool PublicKnowledgeKind(const std::string& kind)
{
    return kind == "finding" || kind == "strategy" || kind == "evaluation" || kind == "adapter" || kind == "generation";
}

bool LoadTextArray(const UniValue& root, const char* key, UniValue& out, std::string& err)
{
    out = UniValue(UniValue::VARR);
    if (!root.exists(key)) return true;
    if (!root[key].isArray()) {
        err = "KNOWLEDGE_JSON";
        return false;
    }
    for (const UniValue& item : root[key].getValues()) {
        if (!item.isStr()) {
            err = "KNOWLEDGE_JSON";
            return false;
        }
        out.push_back(item.get_str());
    }
    return true;
}

void RememberLineage(std::vector<std::string>& lineages, const std::string& lineage)
{
    if (lineage.empty()) return;
    for (const std::string& known : lineages) {
        if (known == lineage) return;
    }
    lineages.push_back(lineage);
}

} // namespace

bool SwarmPublishLive(const fs::path& modeldir, std::string& err)
{
    const fs::path contribution = modeldir / "swarm" / "contribution.json";
    if (!fs::exists(contribution)) {
        err.clear();
        return true;
    }
    std::string raw;
    if (!ReadRaw(contribution, raw)) {
        err = "CONTRIBUTION_READ";
        return false;
    }
    UniValue root;
    if (!root.read(raw) || !root.isObject() || !root.exists("models") || !root["models"].isArray()) {
        err = "CONTRIBUTION_JSON";
        return false;
    }

    SwarmNetworks networks;
    networks.automatic_spend_atoms = "0";
    std::vector<std::string> lineages;
    for (const UniValue& item : root["models"].getValues()) {
        if (!item.isObject()) continue;
        if (!item.exists("acquire") || !item["acquire"].isStr() || item["acquire"].get_str() != "joined") continue;
        if (!item.exists("artifact_id") || !item["artifact_id"].isStr()) continue;
        const std::string artifact = item["artifact_id"].get_str();
        if (artifact.empty()) continue;
        networks.family.member_artifact_ids.push_back(artifact);
        if (item.exists("lineage_id") && item["lineage_id"].isStr()) {
            const std::string lineage = item["lineage_id"].get_str();
            if (networks.family.lineage_id.empty()) networks.family.lineage_id = lineage;
            RememberLineage(lineages, lineage);
        }
        const bool cohort = item.exists("cohort") && item["cohort"].isTrue();
        const std::string quantization =
            item.exists("quantization") && item["quantization"].isStr() ? item["quantization"].get_str() : "";
        // A 4-bit row stays in the family and stays out of the cohort.
        if (!cohort || FourBit(quantization)) continue;
        networks.cohort.member_ids.push_back(artifact);
        if (networks.cohort.checkpoint_id.empty() && item.exists("checkpoint_id") && item["checkpoint_id"].isStr()) {
            networks.cohort.checkpoint_id = item["checkpoint_id"].get_str();
        }
    }

    if (!SwarmStoreNetworks(modeldir, networks, err)) return false;

    fs::path swarm_dir;
    if (!EnsureDir(modeldir, swarm_dir, err, "TIMESCALE_DIR")) return false;
    UniValue immediate(UniValue::VARR);
    UniValue short_term(UniValue::VARR);
    UniValue medium(UniValue::VARR);
    UniValue long_term(UniValue::VARR);
    for (const std::string& lineage : lineages) {
        UniValue row(UniValue::VOBJ);
        row.pushKV("lineage_id", lineage);
        long_term.push_back(std::move(row));
    }
    UniValue times(UniValue::VOBJ);
    times.pushKV("automatic_spend_atoms", "0");
    times.pushKV("general_stable", "unchanged");
    times.pushKV("immediate", immediate);
    times.pushKV("long", long_term);
    times.pushKV("medium", medium);
    times.pushKV("short", short_term);
    return WriteAtomic(swarm_dir / "timescales.json", times.write(0, 0) + "\n", err, "TIMESCALE_WRITE",
                       "TIMESCALE_RENAME");
}

bool SwarmStorePublicKnowledge(const fs::path& modeldir, const std::string& kind, const std::string& body,
                               std::string& err)
{
    if (PrivateKnowledge(kind)) {
        err = "PRIVATE_DATA";
        return false;
    }
    if (!PublicKnowledgeKind(kind)) {
        err = "KNOWLEDGE_KIND";
        return false;
    }

    fs::path swarm_dir;
    if (!EnsureDir(modeldir, swarm_dir, err, "KNOWLEDGE_DIR")) return false;
    const fs::path path = swarm_dir / "knowledge.json";
    UniValue finding(UniValue::VARR);
    UniValue strategy(UniValue::VARR);
    UniValue evaluation(UniValue::VARR);
    UniValue adapter(UniValue::VARR);
    UniValue generation(UniValue::VARR);
    if (fs::exists(path)) {
        std::string raw;
        UniValue existing;
        if (!ReadRaw(path, raw) || !existing.read(raw) || !existing.isObject()) {
            err = "KNOWLEDGE_JSON";
            return false;
        }
        if (!LoadTextArray(existing, "finding", finding, err) || !LoadTextArray(existing, "strategy", strategy, err) ||
            !LoadTextArray(existing, "evaluation", evaluation, err) ||
            !LoadTextArray(existing, "adapter", adapter, err) ||
            !LoadTextArray(existing, "generation", generation, err)) {
            return false;
        }
    }

    // Text under the kind key. Not a digest list and not a private prompt log.
    if (kind == "finding") finding.push_back(body);
    else if (kind == "strategy") strategy.push_back(body);
    else if (kind == "evaluation") evaluation.push_back(body);
    else if (kind == "adapter") adapter.push_back(body);
    else generation.push_back(body);

    UniValue root(UniValue::VOBJ);
    root.pushKV("adapter", adapter);
    root.pushKV("automatic_spend_atoms", "0");
    root.pushKV("evaluation", evaluation);
    root.pushKV("finding", finding);
    root.pushKV("generation", generation);
    root.pushKV("private_prompt_log", false);
    root.pushKV("strategy", strategy);
    return WriteAtomic(path, root.write(0, 0) + "\n", err, "KNOWLEDGE_WRITE", "KNOWLEDGE_RENAME");
}

} // namespace modelnet
