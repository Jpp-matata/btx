// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/nif_observe/nif_observe.h>
#include <modelnet/swarm_bound/swarm_bound.h>
#include <modelnet/tick_bind/tick_bind.h>
#include <modelnet/swarm_form/plane.h>
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
#include <cstdint>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace modelnet {
namespace {

constexpr uint64_t kCatalogLimit = 32ull * 1024ull * 1024ull;

using Row = SwarmCatalogEntry;

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

std::string DigestBytes(const std::string& raw)
{
    CSHA384 hasher;
    hasher.Write(reinterpret_cast<const unsigned char*>(raw.data()), raw.size());
    unsigned char out[CSHA384::OUTPUT_SIZE];
    hasher.Finalize(out);
    return HexStr(Span<const unsigned char>(out, CSHA384::OUTPUT_SIZE));
}

bool Fingerprint(const fs::path& path, std::string& out, std::string& err)
{
    out.clear();
    if (!fs::exists(path)) {
        out = "missing";
        return true;
    }
    std::error_code ec;
    const auto sz = fs::file_size(path, ec);
    if (ec) {
        err = "CATALOG_READ";
        return false;
    }
    if (sz > kCatalogLimit) {
        out = "limit";
        return true;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "CATALOG_READ";
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out = DigestBytes(raw);
    return true;
}

struct InputGeneration {
    bool seen{false};
    std::string catalog;
    std::string mappings;
    std::string device;
    std::string peers;
};

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

bool SwarmReadCatalog(const fs::path& modeldir, SwarmCatalog& entries, std::string& err)
{
    entries.clear();
    err.clear();
    const fs::path catalog = modeldir / "catalog.json";
    const fs::path mappings = modeldir / "swarm" / "lineage-mappings.json";
    if (!RejectSymlink(catalog, err)) return false;
    if (!fs::exists(catalog)) return true;
    std::string raw;
    if (!ReadBounded(catalog, raw, err)) return false;
    UniValue root;
    if (!root.read(raw) || !root.isObject() || !root.exists("models") || !root["models"].isArray()) {
        err = "CATALOG_SCHEMA";
        return false;
    }
    UniValue map_root(UniValue::VOBJ);
    if (!RejectSymlink(mappings, err)) return false;
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
        const bool complete = item.exists("incomplete") && item["incomplete"].isBool() && !item["incomplete"].get_bool();
        if (!verified || !complete) continue;
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
            if (mapping.exists("visibility") && !mapping["visibility"].isStr()) {
                err = "VISIBILITY";
                return false;
            }
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
        for (const char* field : {"quantization", "checkpoint_id", "recipe"}) {
            if (item.exists(field) && !item[field].isStr()) {
                err = "CATALOG_SCHEMA";
                return false;
            }
        }
        if (item.exists("quantization")) row.quantization = item["quantization"].get_str();
        if (item.exists("checkpoint_id")) row.checkpoint = item["checkpoint_id"].get_str();
        if (item.exists("recipe")) row.recipe = item["recipe"].get_str();
        if (!fresh.emplace(row.artifact_id, row).second) {
            err = "CATALOG_DUPLICATE";
            return false;
        }
    }
    entries = std::move(fresh);
    return true;
}

bool SwarmObserveCatalog(const fs::path& modeldir, std::string& err)
{
    SwarmCatalog fresh;
    if (!SwarmReadCatalog(modeldir, fresh, err)) return false;
    const fs::path swarm_dir = modeldir / "swarm";
    const fs::path stored_path = swarm_dir / "observers.json";
    std::map<std::string, Row> rows;
    if (fs::exists(stored_path)) {
        if (!LoadStored(stored_path, rows, err)) return false;
    }
    bool changed = rows.size() != fresh.size();
    for (const auto& [id, row] : fresh) {
        const auto it = rows.find(id);
        if (it == rows.end() || it->second.model_id != row.model_id || it->second.lineage_id != row.lineage_id ||
            it->second.lineage_assurance != row.lineage_assurance || it->second.visibility != row.visibility) {
            changed = true;
        }
    }
    if (!changed && fs::exists(stored_path)) return true;
    if (fresh.empty() && rows.empty()) return true;
    return WriteStored(swarm_dir, fresh, err);
}

void StartSwarmObserver(const fs::path& modeldir)
{
    StopSwarmObserver();
    if (modeldir.empty()) return;
    g_stop.store(false);
    g_thread = std::thread([modeldir] {
        NifObserve nif_observe;
        SwarmBound swarm_bound;
        TickBind tick_bind;
        int64_t nif_now_ms = 0;
        InputGeneration generation;
        while (!g_stop.load()) {
            std::string err;
            bool ok = true;
            try {
                std::lock_guard<std::mutex> lock(g_mu);
                InputGeneration now;
                if (!Fingerprint(modeldir / "catalog.json", now.catalog, err) ||
                    !Fingerprint(modeldir / "swarm" / "lineage-mappings.json", now.mappings, err) ||
                    !Fingerprint(modeldir / "swarm" / "device.json", now.device, err) ||
                    !Fingerprint(modeldir / "swarm" / "peers.json", now.peers, err)) {
                    ok = false;
                } else {
                    const bool catalog_changed = !generation.seen || now.catalog != generation.catalog ||
                                                 now.mappings != generation.mappings;
                    const bool mesh_changed = !generation.seen || now.device != generation.device || now.peers != generation.peers;
                    bool retain = true;
                    if (catalog_changed) {
                        ok = SwarmObserveCatalog(modeldir, err);
                        if (!ok) retain = false;
                    }
                    if (ok && (catalog_changed || mesh_changed)) {
                        std::string mesh_err;
                        if (!SwarmRefreshLocalMesh(modeldir, mesh_err)) {
                            LogPrintf("swarm mesh: %s (monetary node continues)\n", mesh_err);
                            retain = false;
                        }
                        std::string join_err;
                        if (!SwarmIntegrateCatalog(modeldir, join_err)) {
                            LogPrintf("swarm join: %s (monetary node continues)\n", join_err);
                            retain = false;
                        } else {
                            const fs::path membership = modeldir / "swarm" / kMembershipFile;
                            if (catalog_changed || !fs::exists(membership)) {
                                std::string form_err;
                                if (!SwarmFormPlane(modeldir, form_err)) {
                                    LogPrintf("swarm form: %s (monetary node continues)\n", form_err);
                                    retain = false;
                                }
                            }
                            std::string live_err;
                            if (!SwarmPublishLive(modeldir, live_err)) {
                                LogPrintf("swarm live: %s (monetary node continues)\n", live_err);
                                retain = false;
                            }
                        }
                    }
                    if (ok && retain) {
                        now.seen = true;
                        generation = std::move(now);
                    }
                }
                std::string nif_err;
                if (!nif_observe.Tick(nif_now_ms, nif_err)) {
                    LogPrintf("swarm nif observe: %s (monetary node continues)\n", nif_err);
                }
                std::string bound_err;
                if (!swarm_bound.Pass(nif_now_ms, bound_err)) {
                    LogPrintf("swarm bound: %s (monetary node continues)\n", bound_err);
                }
                std::string tick_err;
                if (!tick_bind.Pass(tick_err)) {
                    LogPrintf("swarm tick bind: %s (monetary node continues)\n", tick_err);
                }
                if (nif_now_ms < INT64_MAX) ++nif_now_ms;
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
