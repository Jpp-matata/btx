// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/lab_disk.h>

#include <univalue.h>
#include <util/fs_helpers.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace modelnet {
namespace {

constexpr uint64_t kMaxDiskBytes = 32ull * 1024ull * 1024ull;

bool Fail(std::string& err, const char* code)
{
    err = code;
    return false;
}

bool HexEven(const std::string& text)
{
    if (text.size() % 2 != 0) return false;
    for (unsigned char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool ParseU64(const std::string& text, uint64_t& out)
{
    if (text.empty() || text.size() > 20) return false;
    if (text.size() > 1 && text[0] == '0') return false;
    uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

bool ParseI64(const std::string& text, int64_t& out)
{
    if (text.empty() || text.size() > 20) return false;
    size_t i = 0;
    bool neg = false;
    if (text[0] == '-') {
        neg = true;
        i = 1;
        if (i == text.size()) return false;
    }
    if (text[i] == '0' && i + 1 != text.size()) return false;
    uint64_t mag = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(text[i] - '0');
        if (mag > (UINT64_MAX - digit) / 10) return false;
        mag = mag * 10 + digit;
    }
    if (!neg) {
        if (mag > static_cast<uint64_t>(INT64_MAX)) return false;
        out = static_cast<int64_t>(mag);
        return true;
    }
    if (mag > static_cast<uint64_t>(INT64_MAX) + 1) return false;
    if (mag == static_cast<uint64_t>(INT64_MAX) + 1) {
        out = INT64_MIN;
        return true;
    }
    out = -static_cast<int64_t>(mag);
    return true;
}

bool ReadStringMap(const UniValue& obj, std::map<std::string, std::string>& out)
{
    if (!obj.isObject()) return false;
    std::map<std::string, UniValue> items;
    obj.getObjMap(items);
    for (const auto& item : items) {
        if (!item.second.isStr()) return false;
        out.emplace(item.first, item.second.get_str());
    }
    return true;
}

bool ReadStringSet(const UniValue& arr, std::set<std::string>& out)
{
    if (!arr.isArray()) return false;
    for (size_t i = 0; i < arr.size(); ++i) {
        if (!arr[i].isStr()) return false;
        out.insert(arr[i].get_str());
    }
    return true;
}

bool FieldStr(const UniValue& obj, const char* key, std::string& out)
{
    if (!obj.exists(key) || !obj[key].isStr()) return false;
    out = obj[key].get_str();
    return true;
}

bool ReadOptionalString(const UniValue& obj, const char* key, std::string& out)
{
    if (!obj.exists(key)) {
        out.clear();
        return true;
    }
    if (!obj[key].isStr()) return false;
    out = obj[key].get_str();
    return true;
}

bool ReadOptionalIdList(const UniValue& obj, const char* key, std::vector<std::string>& out)
{
    out.clear();
    if (!obj.exists(key)) return true;
    if (!obj[key].isArray()) return false;
    for (size_t i = 0; i < obj[key].size(); ++i) {
        if (!obj[key][i].isStr()) return false;
        out.push_back(obj[key][i].get_str());
    }
    return true;
}

bool ReadObservations(const UniValue& arr, std::vector<ResearchObservation>& out)
{
    if (!arr.isArray()) return false;
    for (size_t i = 0; i < arr.size(); ++i) {
        const UniValue& row = arr[i];
        if (!row.isObject()) return false;
        ResearchObservation observation;
        std::string sequence;
        std::string reproduction;
        std::string contest;
        if (!FieldStr(row, "record_id", observation.record_id) || observation.record_id.empty() ||
            !FieldStr(row, "topic_id", observation.topic_id) ||
            !FieldStr(row, "author_id", observation.author_id) ||
            !FieldStr(row, "content_hash", observation.content_hash) ||
            !row.exists("negative") || !row["negative"].isBool() ||
            !row.exists("is_private") || !row["is_private"].isBool() ||
            !FieldStr(row, "sequence", sequence) || !FieldStr(row, "reproduction_count", reproduction) ||
            !FieldStr(row, "contest_count", contest)) {
            return false;
        }
        uint64_t sequence_n = 0;
        uint64_t reproduction_n = 0;
        uint64_t contest_n = 0;
        if (!ParseU64(sequence, sequence_n) || !ParseU64(reproduction, reproduction_n) || !ParseU64(contest, contest_n)) return false;
        if (reproduction_n > UINT32_MAX || contest_n > UINT32_MAX) return false;
        observation.negative = row["negative"].get_bool();
        observation.is_private = row["is_private"].get_bool();
        observation.sequence = sequence_n;
        observation.reproduction_count = static_cast<uint32_t>(reproduction_n);
        observation.contest_count = static_cast<uint32_t>(contest_n);
        if (!ReadOptionalString(row, "kind", observation.kind) ||
            !ReadOptionalIdList(row, "physical_parent_ids", observation.physical_parent_ids) ||
            !ReadOptionalIdList(row, "informed_by", observation.informed_by)) {
            return false;
        }
        out.push_back(std::move(observation));
    }
    return true;
}

bool ReadRepoObjects(const UniValue& obj, std::map<std::string, std::vector<LabDiskObject>>& out)
{
    if (!obj.isObject()) return false;
    std::map<std::string, UniValue> repos;
    obj.getObjMap(repos);
    for (const auto& repo : repos) {
        if (repo.first.empty() || !repo.second.isArray()) return false;
        std::vector<LabDiskObject> objects;
        for (size_t i = 0; i < repo.second.size(); ++i) {
            const UniValue& row = repo.second[i];
            if (!row.isObject()) return false;
            LabDiskObject object;
            if (!FieldStr(row, "kind", object.kind) || object.kind.empty() ||
                !FieldStr(row, "digest", object.digest) || !FieldStr(row, "content", object.content)) {
                return false;
            }
            objects.push_back(std::move(object));
        }
        out.emplace(repo.first, std::move(objects));
    }
    return true;
}

bool ReadTrains(const UniValue& obj, std::map<std::string, IntegerState>& out)
{
    if (!obj.isObject()) return false;
    std::map<std::string, UniValue> items;
    obj.getObjMap(items);
    for (const auto& item : items) {
        if (item.first.size() != 96 || !HexEven(item.first) || !item.second.isObject()) return false;
        const UniValue& row = item.second;
        IntegerState state;
        std::string weight;
        std::string bias;
        std::string momentum_w;
        std::string momentum_b;
        std::string cursor;
        std::string step;
        if (!FieldStr(row, "weight", weight) || !FieldStr(row, "bias", bias) ||
            !FieldStr(row, "momentum_w", momentum_w) || !FieldStr(row, "momentum_b", momentum_b) ||
            !FieldStr(row, "cursor", cursor) || !FieldStr(row, "step", step) ||
            !FieldStr(row, "state_hash", state.state_hash)) {
            return false;
        }
        if (!ParseI64(weight, state.weight) || !ParseI64(bias, state.bias) ||
            !ParseI64(momentum_w, state.momentum_w) || !ParseI64(momentum_b, state.momentum_b) ||
            !ParseU64(cursor, state.cursor) || !ParseU64(step, state.step)) {
            return false;
        }
        if (state.state_hash.size() != 96 || !HexEven(state.state_hash)) return false;
        out.emplace(item.first, std::move(state));
    }
    return true;
}

bool DecodeImage(const std::string& json, LabDiskImage& image)
{
    UniValue root;
    if (!root.read(json) || !root.isObject()) return false;
    if (root.exists("identity_sk")) return false;
    if (!root.exists("automatic_spend_atoms") || !root["automatic_spend_atoms"].isStr() ||
        root["automatic_spend_atoms"].get_str() != "0") {
        return false;
    }
    LabDiskImage loaded;
    if (!root.exists("records") || !ReadStringMap(root["records"], loaded.records)) return false;
    if (!root.exists("observations") || !ReadObservations(root["observations"], loaded.observations)) return false;
    if (!root.exists("repo_objects") || !ReadRepoObjects(root["repo_objects"], loaded.repo_objects)) return false;
    if (!root.exists("repos") || !ReadStringSet(root["repos"], loaded.repos)) return false;
    if (!root.exists("snapshots") || !ReadStringSet(root["snapshots"], loaded.snapshots)) return false;
    if (!root.exists("realms") || !ReadStringMap(root["realms"], loaded.realms)) return false;
    if (!root.exists("lineage") || !ReadStringMap(root["lineage"], loaded.lineage)) return false;
    if (!root.exists("plans") || !ReadStringMap(root["plans"], loaded.plans)) return false;
    if (!root.exists("admissions") || !ReadStringMap(root["admissions"], loaded.admissions)) return false;
    if (root.exists("identity_pk")) {
        if (!ReadStringMap(root["identity_pk"], loaded.identity_pk)) return false;
        for (const auto& item : loaded.identity_pk) {
            if (!HexEven(item.second)) return false;
        }
    }
    if (!root.exists("trains") || !ReadTrains(root["trains"], loaded.trains)) return false;
    image = std::move(loaded);
    return true;
}

UniValue StringMap(const std::map<std::string, std::string>& values)
{
    UniValue obj(UniValue::VOBJ);
    for (const auto& item : values) obj.pushKV(item.first, item.second);
    return obj;
}

UniValue StringSet(const std::set<std::string>& values)
{
    UniValue arr(UniValue::VARR);
    for (const std::string& value : values) arr.push_back(value);
    return arr;
}

std::string EncodeImage(const LabDiskImage& image)
{
    UniValue observations(UniValue::VARR);
    for (const ResearchObservation& observation : image.observations) {
        UniValue row(UniValue::VOBJ);
        row.pushKV("record_id", observation.record_id);
        row.pushKV("topic_id", observation.topic_id);
        row.pushKV("author_id", observation.author_id);
        row.pushKV("content_hash", observation.content_hash);
        row.pushKV("negative", observation.negative);
        row.pushKV("is_private", observation.is_private);
        row.pushKV("sequence", std::to_string(observation.sequence));
        row.pushKV("reproduction_count", std::to_string(observation.reproduction_count));
        row.pushKV("contest_count", std::to_string(observation.contest_count));
        row.pushKV("kind", observation.kind);
        UniValue physical(UniValue::VARR);
        for (const std::string& id : observation.physical_parent_ids) physical.push_back(id);
        row.pushKV("physical_parent_ids", physical);
        UniValue informed(UniValue::VARR);
        for (const std::string& id : observation.informed_by) informed.push_back(id);
        row.pushKV("informed_by", informed);
        observations.push_back(std::move(row));
    }
    UniValue repos(UniValue::VOBJ);
    for (const auto& repo : image.repo_objects) {
        UniValue objects(UniValue::VARR);
        for (const LabDiskObject& object : repo.second) {
            UniValue row(UniValue::VOBJ);
            row.pushKV("kind", object.kind);
            row.pushKV("digest", object.digest);
            row.pushKV("content", object.content);
            objects.push_back(std::move(row));
        }
        repos.pushKV(repo.first, std::move(objects));
    }
    UniValue root(UniValue::VOBJ);
    root.pushKV("automatic_spend_atoms", "0");
    root.pushKV("records", StringMap(image.records));
    root.pushKV("observations", observations);
    root.pushKV("repo_objects", repos);
    root.pushKV("repos", StringSet(image.repos));
    root.pushKV("snapshots", StringSet(image.snapshots));
    root.pushKV("realms", StringMap(image.realms));
    root.pushKV("lineage", StringMap(image.lineage));
    root.pushKV("plans", StringMap(image.plans));
    root.pushKV("admissions", StringMap(image.admissions));
    root.pushKV("identity_pk", StringMap(image.identity_pk));
    UniValue trains(UniValue::VOBJ);
    for (const auto& item : image.trains) {
        UniValue row(UniValue::VOBJ);
        row.pushKV("weight", std::to_string(item.second.weight));
        row.pushKV("bias", std::to_string(item.second.bias));
        row.pushKV("momentum_w", std::to_string(item.second.momentum_w));
        row.pushKV("momentum_b", std::to_string(item.second.momentum_b));
        row.pushKV("cursor", std::to_string(item.second.cursor));
        row.pushKV("step", std::to_string(item.second.step));
        row.pushKV("state_hash", item.second.state_hash);
        trains.pushKV(item.first, std::move(row));
    }
    root.pushKV("trains", trains);
    return root.write();
}

bool ReadAll(const fs::path& path, std::string& out, std::string& err)
{
    FILE* file = fsbridge::fopen(path, "rb");
    if (!file) return Fail(err, "BTX035_RECOVERY_REQUIRED");
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return Fail(err, "BTX035_RECOVERY_REQUIRED");
    }
    const long size = ftell(file);
    if (size < 0 || static_cast<uint64_t>(size) > kMaxDiskBytes) {
        fclose(file);
        return Fail(err, "BTX035_RECOVERY_REQUIRED");
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return Fail(err, "BTX035_RECOVERY_REQUIRED");
    }
    out.resize(static_cast<size_t>(size));
    const size_t n = size == 0 ? 0 : fread(out.data(), 1, out.size(), file);
    const int closed = fclose(file);
    if (closed != 0 || n != out.size()) return Fail(err, "BTX035_RECOVERY_REQUIRED");
    return true;
}

bool AtomicWrite(const fs::path& dir, const std::string& bytes, std::string& err)
{
    const fs::path dest = dir / "lab.json";
    const fs::path tmp = dir / "lab.json.tmp";
    FILE* file = fsbridge::fopen(tmp, "wb");
    if (!file) return Fail(err, "BTX035_RECOVERY_REQUIRED");
    const size_t n = bytes.empty() ? 0 : fwrite(bytes.data(), 1, bytes.size(), file);
    const bool committed = n == bytes.size() && FileCommit(file);
    const int closed = fclose(file);
    if (!committed || closed != 0 || !RenameOver(tmp, dest)) {
        std::error_code ec;
        fs::remove(tmp, ec);
        return Fail(err, "BTX035_RECOVERY_REQUIRED");
    }
    DirectoryCommit(dir);
    err.clear();
    return true;
}

bool EnsureDir(const fs::path& dir, std::string& err)
{
    try {
        if (fs::exists(dir)) {
            if (!fs::is_directory(dir)) return Fail(err, "BTX035_RECOVERY_REQUIRED");
            return true;
        }
        if (!fs::create_directories(dir)) return Fail(err, "BTX035_RECOVERY_REQUIRED");
    } catch (const std::filesystem::filesystem_error&) {
        return Fail(err, "BTX035_RECOVERY_REQUIRED");
    }
    return true;
}

} // namespace

fs::path LabDiskDirectory()
{
    const char* env = std::getenv("BTX035_LAB_DIR");
    if (env == nullptr || env[0] == '\0') return {};
    return fs::path(env);
}

bool LoadLabDisk(LabDiskImage& image, std::string& err)
{
    const fs::path dir = LabDiskDirectory();
    if (dir.empty()) {
        err.clear();
        return true;
    }
    bool present = false;
    try {
        present = fs::exists(dir);
        if (present && !fs::is_directory(dir)) return Fail(err, "BTX035_RECOVERY_REQUIRED");
    } catch (const std::filesystem::filesystem_error&) {
        return Fail(err, "BTX035_RECOVERY_REQUIRED");
    }
    if (!present) {
        err.clear();
        return true;
    }
    const fs::path file = dir / "lab.json";
    bool have_file = false;
    try {
        have_file = fs::exists(file);
    } catch (const std::filesystem::filesystem_error&) {
        return Fail(err, "BTX035_RECOVERY_REQUIRED");
    }
    if (!have_file) {
        err.clear();
        return true;
    }
    std::string bytes;
    if (!ReadAll(file, bytes, err)) return false;
    if (!DecodeImage(bytes, image)) return Fail(err, "BTX035_RECOVERY_REQUIRED");
    err.clear();
    return true;
}

bool SaveLabDisk(const LabDiskImage& image, std::string& err)
{
    const fs::path dir = LabDiskDirectory();
    if (dir.empty()) {
        err.clear();
        return true;
    }
    if (!EnsureDir(dir, err)) return false;
    const std::string bytes = EncodeImage(image);
    if (bytes.size() > kMaxDiskBytes) return Fail(err, "BTX035_LIMIT_EXCEEDED");
    return AtomicWrite(dir, bytes, err);
}

} // namespace modelnet
