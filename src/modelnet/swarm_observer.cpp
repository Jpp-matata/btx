// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_lifecycle.h>
#include <modelnet/swarm_live.h>
#include <modelnet/swarm_node.h>
#include <modelnet/swarm_observer.h>

#include <crypto/sha384.h>
#include <logging.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace modelnet {
namespace {

constexpr uint64_t kCatalogLimit = 32ull * 1024ull * 1024ull;

struct Row {
    std::string artifact_id;
    std::string model_id;
    std::string lineage_id;
    std::string lineage_assurance;
    std::string visibility;
};

std::atomic<bool> g_stop{false};
std::thread g_thread;
std::mutex g_mu;

bool IsSwarmId(std::string_view s)
{
    if (s.size() != 96) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool RejectSymlink(const fs::path& path, std::string& err)
{
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(path, ec);
    if (ec) return true;
    if (fs::is_symlink(st)) {
        err = "CATALOG_SYMLINK";
        return false;
    }
    return true;
}

bool ReadBounded(const fs::path& path, std::string& raw, std::string& err)
{
    if (!RejectSymlink(path, err)) return false;
    if (!fs::exists(path)) return true;
    std::error_code ec;
    const auto sz = fs::file_size(path, ec);
    if (ec || sz > kCatalogLimit) {
        err = "CATALOG_LIMIT";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "CATALOG_READ";
        return false;
    }
    raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

UniValue RowJson(const Row& row)
{
    UniValue o(UniValue::VOBJ);
    o.pushKV("artifact_id", row.artifact_id);
    o.pushKV("automatic_activation", "FOLLOW_LOCAL_POLICY");
    o.pushKV("channel", "general/stable");
    o.pushKV("discovery", true);
    o.pushKV("idle_compute_authorized", false);
    o.pushKV("lineage_assurance", row.lineage_assurance);
    o.pushKV("lineage_id", row.lineage_id);
    o.pushKV("model_id", row.model_id);
    o.pushKV("private_data_training", false);
    o.pushKV("role", "observer");
    o.pushKV("visibility", row.visibility);
    return o;
}

bool LoadStored(const fs::path& path, std::map<std::string, Row>& rows, std::string& err)
{
    std::string raw;
    if (!ReadBounded(path, raw, err)) return false;
    if (raw.empty()) return true;
    UniValue root;
    if (!root.read(raw) || !root.isObject() || !root.exists("subscriptions") || !root["subscriptions"].isArray()) {
        err = "OBSERVER_SCHEMA";
        return false;
    }
    for (const UniValue& item : root["subscriptions"].getValues()) {
        if (!item.isObject()) {
            err = "OBSERVER_SCHEMA";
            return false;
        }
        Row row;
        row.artifact_id = item["artifact_id"].get_str();
        row.model_id = item["model_id"].get_str();
        row.lineage_id = item["lineage_id"].get_str();
        row.lineage_assurance = item["lineage_assurance"].get_str();
        row.visibility = item["visibility"].get_str();
        if (!IsSwarmId(row.artifact_id) || !IsSwarmId(row.model_id) || !IsSwarmId(row.lineage_id)) {
            err = "ID";
            return false;
        }
        rows.insert_or_assign(row.artifact_id, std::move(row));
    }
    return true;
}

bool WriteStored(const fs::path& dir, const std::map<std::string, Row>& rows, std::string& err)
{
    fs::create_directories(dir);
    if (!fs::exists(dir)) {
        err = "OBSERVER_DIR";
        return false;
    }
    std::error_code ec;
    UniValue subs(UniValue::VARR);
    for (const auto& [id, row] : rows) {
        (void)id;
        subs.push_back(RowJson(row));
    }
    UniValue root(UniValue::VOBJ);
    root.pushKV("automatic_spend_atoms", "0");
    root.pushKV("payment_authority", false);
    root.pushKV("subscriptions", subs);
    const fs::path final_path = dir / "observers.json";
    const fs::path tmp = dir / "observers.json.tmp";
    if (!RejectSymlink(final_path, err) || !RejectSymlink(tmp, err)) return false;
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            err = "OBSERVER_WRITE";
            return false;
        }
        out << root.write(0, 0) << "\n";
        out.close();
        if (!out) {
            err = "OBSERVER_WRITE";
            return false;
        }
    }
    fs::rename(tmp, final_path, ec);
    if (ec) {
        err = "OBSERVER_RENAME";
        return false;
    }
    return true;
}

} // namespace

std::string SwarmProvisionalLineage(const std::string& model_id)
{
    const std::string body = "{\"model_id\":\"" + model_id + "\"}";
    static constexpr char domain[] = "BTX/ProvisionalModelTopic/1";
    CSHA384 hasher;
    hasher.Write(reinterpret_cast<const unsigned char*>(domain), sizeof(domain) - 1);
    const unsigned char sep = 0;
    hasher.Write(&sep, 1);
    hasher.Write(reinterpret_cast<const unsigned char*>(body.data()), body.size());
    unsigned char out[CSHA384::OUTPUT_SIZE];
    hasher.Finalize(out);
    return HexStr(Span<const unsigned char>(out, CSHA384::OUTPUT_SIZE));
}

bool SwarmObserveCatalog(const fs::path& modeldir, std::string& err)
{
    const fs::path catalog = modeldir / "catalog.json";
    const fs::path swarm_dir = modeldir / "swarm";
    const fs::path mappings = swarm_dir / "lineage-mappings.json";
    const fs::path stored_path = swarm_dir / "observers.json";
    if (!fs::exists(catalog)) return true;
    std::string raw;
    if (!ReadBounded(catalog, raw, err)) return false;
    UniValue root;
    if (!root.read(raw) || !root.isObject() || !root.exists("models") || !root["models"].isArray()) {
        err = "CATALOG_SCHEMA";
        return false;
    }
    UniValue map_root(UniValue::VOBJ);
    if (fs::exists(mappings)) {
        std::string map_raw;
        if (!ReadBounded(mappings, map_raw, err)) return false;
        if (!map_raw.empty()) {
            if (!map_root.read(map_raw) || !map_root.isObject()) {
                err = "MAPPING_SCHEMA";
                return false;
            }
        }
    }
    std::map<std::string, Row> fresh;
    for (const UniValue& item : root["models"].getValues()) {
        if (!item.isObject()) {
            err = "CATALOG_SCHEMA";
            return false;
        }
        const bool verified = item.exists("bytes_verified") && item["bytes_verified"].isBool() && item["bytes_verified"].get_bool();
        const bool incomplete = item.exists("incomplete") && item["incomplete"].isBool() && item["incomplete"].get_bool();
        if (!verified || incomplete) continue;
        if (!item.exists("artifact_id") || !item.exists("model_id") || !item["artifact_id"].isStr() || !item["model_id"].isStr()) {
            err = "ID";
            return false;
        }
        Row row;
        row.artifact_id = item["artifact_id"].get_str();
        row.model_id = item["model_id"].get_str();
        if (!IsSwarmId(row.artifact_id) || !IsSwarmId(row.model_id)) {
            err = "ID";
            return false;
        }
        if (map_root.exists(row.artifact_id)) {
            const UniValue& mapping = map_root[row.artifact_id];
            if (!mapping.isObject() || !mapping.exists("lineage_id") || !mapping["lineage_id"].isStr()) {
                err = "MAPPING_SCHEMA";
                return false;
            }
            row.lineage_id = mapping["lineage_id"].get_str();
            row.visibility = mapping.exists("visibility") ? mapping["visibility"].get_str() : "private";
            if (!IsSwarmId(row.lineage_id) || (row.visibility != "private" && row.visibility != "public")) {
                err = "VISIBILITY";
                return false;
            }
            row.lineage_assurance = "VERIFIED_MAPPING";
        } else {
            row.lineage_id = SwarmProvisionalLineage(row.model_id);
            row.lineage_assurance = "PROVISIONAL_NOT_AUTHORITATIVE";
            row.visibility = "private";
        }
        fresh.insert_or_assign(row.artifact_id, std::move(row));
    }
    std::map<std::string, Row> rows;
    if (fs::exists(stored_path)) {
        if (!LoadStored(stored_path, rows, err)) return false;
    }
    bool changed = false;
    for (const auto& [id, row] : fresh) {
        const auto it = rows.find(id);
        if (it == rows.end() || it->second.model_id != row.model_id || it->second.lineage_id != row.lineage_id ||
            it->second.lineage_assurance != row.lineage_assurance || it->second.visibility != row.visibility) {
            rows.insert_or_assign(id, row);
            changed = true;
        }
    }
    if (!changed && fs::exists(stored_path)) return true;
    if (fresh.empty() && rows.empty()) return true;
    return WriteStored(swarm_dir, rows, err);
}

void StartSwarmObserver(const fs::path& modeldir)
{
    StopSwarmObserver();
    if (modeldir.empty()) return;
    g_stop.store(false);
    g_thread = std::thread([modeldir] {
        while (!g_stop.load()) {
            std::string err;
            bool ok = false;
            try {
                std::lock_guard<std::mutex> lock(g_mu);
                ok = SwarmObserveCatalog(modeldir, err);
                if (ok) {
                    std::string mesh_err;
                    if (!SwarmRefreshLocalMesh(modeldir, mesh_err)) {
                        LogPrintf("swarm mesh: %s (monetary node continues)\n", mesh_err);
                    }
                    std::string join_err;
                    if (!SwarmIntegrateCatalog(modeldir, join_err)) {
                        LogPrintf("swarm join: %s (monetary node continues)\n", join_err);
                    }
                    std::string live_err;
                    if (!SwarmPublishLive(modeldir, live_err)) {
                        LogPrintf("swarm live: %s (monetary node continues)\n", live_err);
                    }
                }
            } catch (const std::exception& e) {
                ok = false;
                err = e.what();
            }
            if (!ok) LogPrintf("swarm observer: %s (monetary node continues)\n", err);
            for (int i = 0; i < 20 && !g_stop.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });
}

void StopSwarmObserver() noexcept
{
    g_stop.store(true);
    if (g_thread.joinable()) g_thread.join();
}

} // namespace modelnet
