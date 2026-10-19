// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_lifecycle.h>

#include <modelnet/swarm_codec.h>
#include <modelnet/swarm_observer.h>

#include <univalue.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <thread>

namespace modelnet {
namespace {

bool Blocked(const SwarmDevice& device)
{
    return device.battery || device.thermal_hot || device.metered;
}

bool ReadFile(const fs::path& path, std::string& raw)
{
    if (!fs::exists(path)) return true;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

std::vector<SwarmPeer> SwarmNeighbors(const std::vector<SwarmPeer>& peers, int near, int remote)
{
    std::vector<SwarmPeer> eligible;
    for (const SwarmPeer& peer : peers) {
        if (peer.authorized && peer.available) eligible.push_back(peer);
    }
    std::sort(eligible.begin(), eligible.end(), [](const SwarmPeer& a, const SwarmPeer& b) {
        if (a.rtt_us != b.rtt_us) return a.rtt_us < b.rtt_us;
        if (a.bandwidth_bps != b.bandwidth_bps) return a.bandwidth_bps > b.bandwidth_bps;
        return a.id < b.id;
    });
    if (near < 0) near = 0;
    if (remote < 0) remote = 0;
    std::vector<SwarmPeer> chosen;
    chosen.reserve(static_cast<size_t>(near + remote));
    std::set<std::string> used;
    std::set<std::string> domains;
    for (const SwarmPeer& peer : eligible) {
        if (static_cast<int>(chosen.size()) >= near) break;
        chosen.push_back(peer);
        used.insert(peer.id);
        domains.insert(peer.domain);
    }
    std::vector<SwarmPeer> remaining;
    for (const SwarmPeer& peer : eligible) {
        if (!used.count(peer.id)) remaining.push_back(peer);
    }
    std::sort(remaining.begin(), remaining.end(), [&](const SwarmPeer& a, const SwarmPeer& b) {
        const bool a_known = domains.count(a.domain) != 0;
        const bool b_known = domains.count(b.domain) != 0;
        if (a_known != b_known) return !a_known;
        if (a.rtt_us != b.rtt_us) return a.rtt_us < b.rtt_us;
        return a.id < b.id;
    });
    for (const SwarmPeer& peer : remaining) {
        if (static_cast<int>(chosen.size()) >= near + remote) break;
        chosen.push_back(peer);
    }
    return chosen;
}

SwarmRoles SwarmEligibleRoles(const SwarmDevice& device)
{
    SwarmRoles roles;
    const bool idle = device.idle_contribution && !Blocked(device);
    roles.seeder = idle && device.slots > 0;
    roles.evaluator = idle && device.cpu_threads > 0;
    roles.trainer = idle && device.vram_bytes > 0 && device.license_allows_training;
    roles.private_prompts = device.private_prompt_grant;
    roles.private_data = device.private_data_grant;
    roles.automatic_spend_atoms = "0";
    return roles;
}

SwarmBudget SwarmGovern(const SwarmDevice& device, int requested_workers)
{
    SwarmBudget budget;
    if (!device.idle_contribution) {
        budget.code = "IDLE_OFF";
        return budget;
    }
    if (Blocked(device)) {
        budget.code = "RESOURCE_BLOCKED";
        return budget;
    }
    if (requested_workers < 0) requested_workers = 0;
    budget.active_workers = std::min(requested_workers, std::max(device.slots, 0));
    budget.cpu_threads = std::max(device.cpu_threads, 0);
    budget.vram_bytes = std::max<int64_t>(device.vram_bytes, 0);
    budget.max_steps = std::max(device.max_steps, 0);
    budget.drained = budget.active_workers == 0;
    budget.code = budget.drained ? "NO_SLOTS" : "ADMITTED";
    return budget;
}

SwarmJoin SwarmJoinLineage(const std::string& artifact_id, const std::string& model_id,
                           const std::string& checkpoint_id, const std::string& mapped_lineage,
                           const std::string& mapped_visibility)
{
    SwarmJoin join;
    if (!SwarmFixedId(artifact_id) || !SwarmFixedId(model_id) || !SwarmFixedId(checkpoint_id)) {
        join.code = "ID";
        return join;
    }
    join.artifact_id = artifact_id;
    join.model_id = model_id;
    join.checkpoint_id = checkpoint_id;
    if (!mapped_lineage.empty()) {
        if (!SwarmFixedId(mapped_lineage) || (mapped_visibility != "private" && mapped_visibility != "public")) {
            join.code = "VISIBILITY";
            return join;
        }
        join.lineage_id = mapped_lineage;
        join.assurance = "VERIFIED_MAPPING";
        join.visibility = mapped_visibility;
    } else {
        join.lineage_id = SwarmProvisionalLineage(model_id);
        join.assurance = "PROVISIONAL_NOT_AUTHORITATIVE";
        join.visibility = "private";
    }
    if (join.lineage_id == checkpoint_id || join.lineage_id == artifact_id) {
        join.code = "LINEAGE_COLLISION";
        join.ok = false;
        return join;
    }
    join.ok = !join.lineage_id.empty();
    join.code = join.ok ? "JOINED" : "LINEAGE";
    return join;
}

bool SwarmOriginIsLineage(const std::string& origin_url)
{
    (void)origin_url;
    return false;
}

SwarmTimescale SwarmClassifyTimescale(const std::string& kind)
{
    if (kind == "finding" || kind == "kv") return SwarmTimescale::Immediate;
    if (kind == "strategy" || kind == "evaluation") return SwarmTimescale::Short;
    if (kind == "adapter" || kind == "finetune") return SwarmTimescale::Medium;
    return SwarmTimescale::Long;
}

bool SwarmRefreshLocalMesh(const fs::path& modeldir, std::string& err)
{
    const fs::path swarm_dir = modeldir / "swarm";
    fs::create_directories(swarm_dir);
    if (!fs::exists(swarm_dir)) {
        err = "MESH_DIR";
        return false;
    }
    std::vector<SwarmPeer> peers;
    const fs::path peers_path = swarm_dir / "peers.json";
    std::string peers_raw;
    if (fs::exists(peers_path)) {
        if (!ReadFile(peers_path, peers_raw)) {
            err = "PEERS_READ";
            return false;
        }
        UniValue root;
        if (!root.read(peers_raw) || !root.isObject() || !root.exists("peers") || !root["peers"].isArray()) {
            err = "PEERS_JSON";
            return false;
        }
        for (const UniValue& item : root["peers"].getValues()) {
            if (!item.isObject() || !item.exists("id") || !item.exists("domain")) {
                err = "PEERS_JSON";
                return false;
            }
            SwarmPeer peer;
            peer.id = item["id"].get_str();
            peer.domain = item["domain"].get_str();
            peer.authorized = item.exists("authorized") && item["authorized"].isTrue();
            peer.available = item.exists("available") && item["available"].isTrue();
            peer.rtt_us = item.exists("rtt_us") ? item["rtt_us"].getInt<int64_t>() : 0;
            peer.bandwidth_bps = item.exists("bandwidth_bps") ? item["bandwidth_bps"].getInt<int64_t>() : 0;
            peers.push_back(std::move(peer));
        }
    }
    SwarmDevice device;
    const fs::path device_path = swarm_dir / "device.json";
    if (fs::exists(device_path)) {
        std::string raw;
        if (!ReadFile(device_path, raw)) {
            err = "DEVICE_READ";
            return false;
        }
        UniValue root;
        if (!root.read(raw) || !root.isObject()) {
            err = "DEVICE_JSON";
            return false;
        }
        device.idle_contribution = root.exists("idle_contribution") && root["idle_contribution"].isTrue();
        device.slots = root.exists("slots") ? root["slots"].getInt<int>() : 0;
        device.cpu_threads = root.exists("cpu_threads") ? root["cpu_threads"].getInt<int>() : 0;
        device.vram_bytes = root.exists("vram_bytes") ? root["vram_bytes"].getInt<int64_t>() : 0;
        device.max_steps = root.exists("max_steps") ? root["max_steps"].getInt<int>() : 0;
        device.battery = root.exists("battery") && root["battery"].isTrue();
        device.thermal_hot = root.exists("thermal_hot") && root["thermal_hot"].isTrue();
        device.metered = root.exists("metered") && root["metered"].isTrue();
        device.license_allows_training = root.exists("license_allows_training") && root["license_allows_training"].isTrue();
    } else {
        unsigned threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 1;
        if (threads > 64) threads = 64;
        device.idle_contribution = true;
        device.slots = 1;
        device.cpu_threads = static_cast<int>(threads);
        device.vram_bytes = 0;
        device.max_steps = 0;
        device.license_allows_training = false;
    }
    const SwarmRoles roles = SwarmEligibleRoles(device);
    const SwarmBudget budget = SwarmGovern(device, device.slots);
    const std::vector<SwarmPeer> neighborhood = SwarmNeighbors(peers);
    UniValue out(UniValue::VOBJ);
    UniValue ids(UniValue::VARR);
    for (const SwarmPeer& peer : neighborhood) ids.push_back(peer.id);
    out.pushKV("automatic_spend_atoms", "0");
    out.pushKV("drained", budget.drained);
    out.pushKV("evaluator", roles.evaluator);
    out.pushKV("governor", budget.code);
    out.pushKV("neighbors", ids);
    out.pushKV("observer", true);
    out.pushKV("private_data_training", roles.private_data);
    out.pushKV("private_prompts", roles.private_prompts);
    out.pushKV("seeder", roles.seeder);
    out.pushKV("trainer", roles.trainer);
    const fs::path final_path = swarm_dir / "mesh-state.json";
    const fs::path tmp = swarm_dir / "mesh-state.json.tmp";
    {
        std::ofstream file(tmp, std::ios::trunc);
        if (!file) {
            err = "MESH_WRITE";
            return false;
        }
        file << out.write(0, 0) << "\n";
    }
    std::error_code ec;
    fs::rename(tmp, final_path, ec);
    if (ec) {
        err = "MESH_RENAME";
        return false;
    }
    return true;
}

} // namespace modelnet
