// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/git_snapshot.h>
#include <modelnet/guard_machine.h>
#include <modelnet/lab_api.h>
#include <modelnet/lab_exchange.h>
#include <modelnet/research_rpc.h>
#include <span.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

BOOST_FIXTURE_TEST_SUITE(modelnet_035_disk_tests, BasicTestingSetup)

namespace {

struct TempTree {
    explicit TempTree(fs::path path_in) : path(std::move(path_in))
    {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path);
    }
    ~TempTree()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    fs::path path;
};

struct LabDirEnv {
    explicit LabDirEnv(const fs::path& dir) { Point(dir); }
    void Point(const fs::path& dir)
    {
        const std::string text = PathToString(dir);
        ::setenv("BTX035_LAB_DIR", text.c_str(), 1);
    }
    ~LabDirEnv() { ::unsetenv("BTX035_LAB_DIR"); }
};

bool LabCall(const char* method, const UniValue& request, UniValue& result, std::string& err)
{
    std::string response;
    if (!modelnet::DispatchLab035(method, request.write(), response, err)) return false;
    UniValue envelope;
    if (!envelope.read(response) || !envelope.isObject() || !envelope.exists("automatic_spend_atoms") ||
        !envelope["automatic_spend_atoms"].isStr() || envelope["automatic_spend_atoms"].get_str() != "0" ||
        !envelope.exists("result") || !envelope["result"].isObject()) {
        err = "BTX035_SCHEMA";
        return false;
    }
    result = envelope["result"];
    err.clear();
    return true;
}

} // namespace

BOOST_AUTO_TEST_CASE(guard_line_is_one_use_and_refuses_spend)
{
    using namespace modelnet;
    GuardTable table;
    const std::string fingerprint(96, 'a');
    const std::string prepared = GuardHandleLine(table, "PREPARE research.run " + fingerprint + " 1", 1000);
    std::istringstream in(prepared);
    std::string verb;
    std::string secret;
    std::string commitment;
    int64_t not_after = 0;
    in >> verb >> secret >> commitment >> not_after;
    BOOST_CHECK_EQUAL(verb, "PERMIT");
    BOOST_CHECK_EQUAL(commitment, GuardCommitment(secret));
    BOOST_CHECK_EQUAL(not_after, int64_t{1000} + GuardTable::kPermitTtlMs);

    const std::string executed = GuardHandleLine(table, "EXECUTE " + secret, 1000);
    BOOST_CHECK(executed.rfind("RECEIPT ", 0) == 0);
    BOOST_CHECK(executed.size() > std::string("RECEIPT ").size());
    BOOST_CHECK_EQUAL(executed.substr(executed.size() - std::string(" EXECUTED STOPPED_QUIESCENT").size()), " EXECUTED STOPPED_QUIESCENT");

    BOOST_CHECK_EQUAL(GuardHandleLine(table, "EXECUTE " + secret, 1000), "ERR BTX035_REPLAY");
    BOOST_CHECK_EQUAL(GuardHandleLine(table, "PREPARE spend " + std::string(96, 'b') + " 1", 1000), "ERR BTX035_SCOPE_DENIED");
    BOOST_CHECK_EQUAL(GuardHandleLine(table, "PAUSE", 1000), "OK");
    BOOST_CHECK(table.Paused());
    BOOST_CHECK_EQUAL(GuardHandleLine(table, "RESUME", 1000), "OK");
    BOOST_CHECK(!table.Paused());
}

BOOST_AUTO_TEST_CASE(lab_directory_reloads_a_realm_and_a_second_directory_does_not)
{
    BOOST_REQUIRE(!m_path_root.empty());
    ::unsetenv("BTX035_LAB_DIR");
    modelnet::ResetLabApi();

    TempTree first{m_path_root / "btx035-lab-disk" / "a"};
    TempTree second{m_path_root / "btx035-lab-disk" / "b"};
    LabDirEnv env(first.path);
    modelnet::ResetLabApi();

    UniValue create(UniValue::VOBJ);
    create.pushKV("name", "durable");
    create.pushKV("nonce", "nonce-durable-1");
    create.pushKV("policy_template", "local");
    create.pushKV("idempotency_key", "idempotency-disk-realm");
    create.pushKV("expected_state", UniValue());
    UniValue created;
    std::string err;
    BOOST_REQUIRE(LabCall("lab.realm_create", create, created, err));
    const std::string realm = created["realm_id"].get_str();
    const std::string policy = created["policy_id"].get_str();
    BOOST_REQUIRE_EQUAL(realm.size(), 96U);

    UniValue identity(UniValue::VOBJ);
    identity.pushKV("label", "disk");
    identity.pushKV("recovery_key_ids", UniValue(UniValue::VARR));
    identity.pushKV("recovery_threshold", 1);
    identity.pushKV("recovery_delay_ms", "1");
    identity.pushKV("idempotency_key", "idempotency-disk-id");
    identity.pushKV("expected_state", UniValue());
    UniValue identified;
    BOOST_REQUIRE(LabCall("lab.identity_create", identity, identified, err));

    std::ifstream input(PathToString(first.path / "lab.json"));
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string body = buffer.str();
    UniValue disk;
    BOOST_REQUIRE(disk.read(body));
    BOOST_CHECK(!disk.exists("identity_sk"));
    BOOST_CHECK(body.find("identity_sk") == std::string::npos);
    BOOST_REQUIRE(disk["automatic_spend_atoms"].isStr());
    BOOST_CHECK_EQUAL(disk["automatic_spend_atoms"].get_str(), "0");
    BOOST_CHECK(disk["realms"].exists(realm));
    BOOST_REQUIRE(disk["identity_pk"].isObject());
    BOOST_CHECK(!disk["identity_pk"].getKeys().empty());

    modelnet::ReloadLabFromDisk();
    UniValue status_req(UniValue::VOBJ);
    status_req.pushKV("realm_id", realm);
    UniValue status;
    BOOST_REQUIRE(LabCall("lab.realm_status", status_req, status, err));
    BOOST_CHECK_EQUAL(status["policy_id"].get_str(), policy);

    env.Point(second.path);
    modelnet::ReloadLabFromDisk();
    std::string missing;
    BOOST_CHECK(!modelnet::DispatchLab035("lab.realm_status", status_req.write(), missing, err));
    BOOST_CHECK_EQUAL(err, "BTX035_NOT_FOUND");
    BOOST_CHECK(fs::exists(first.path / "lab.json"));
    BOOST_CHECK(!fs::exists(second.path / "lab.json"));

    modelnet::ResetLabApi();
}

BOOST_AUTO_TEST_CASE(snapshot_carries_a_public_negative_row_and_drops_a_private_row)
{
    ::unsetenv("BTX035_LAB_DIR");
    modelnet::ResetLabApi();
    std::string err;
    UniValue identity(UniValue::VOBJ);
    identity.pushKV("label", "research");
    identity.pushKV("recovery_key_ids", UniValue(UniValue::VARR));
    identity.pushKV("recovery_threshold", 1);
    identity.pushKV("recovery_delay_ms", "1");
    identity.pushKV("idempotency_key", "idempotency-disk-research-id");
    identity.pushKV("expected_state", UniValue());
    UniValue identified;
    BOOST_REQUIRE(LabCall("lab.identity_create", identity, identified, err));

    const std::string negative_id(96, 'a');
    const std::string private_id(96, 'c');
    const std::string topic(96, 'b');
    UniValue topics(UniValue::VARR);
    topics.push_back(topic);

    UniValue negative(UniValue::VOBJ);
    negative.pushKV("record_id", negative_id);
    negative.pushKV("topic_ids", topics);
    negative.pushKV("visibility", "NEGATIVE");
    negative.pushKV("idempotency_key", "idempotency-disk-negative");
    negative.pushKV("expected_state", UniValue());
    UniValue published;
    BOOST_REQUIRE(LabCall("lab.research_publish", negative, published, err));

    UniValue hidden(UniValue::VOBJ);
    hidden.pushKV("record_id", private_id);
    hidden.pushKV("topic_ids", topics);
    hidden.pushKV("visibility", "PRIVATE");
    hidden.pushKV("idempotency_key", "idempotency-disk-private");
    hidden.pushKV("expected_state", UniValue());
    BOOST_REQUIRE(LabCall("lab.research_publish", hidden, published, err));

    UniValue repo(UniValue::VOBJ);
    repo.pushKV("label", "source");
    repo.pushKV("nonce", "disk-nonce");
    repo.pushKV("object_format", "git");
    repo.pushKV("project_genesis_id", UniValue());
    repo.pushKV("default_ref", "refs/heads/main");
    repo.pushKV("idempotency_key", "idempotency-disk-repo");
    repo.pushKV("expected_state", UniValue());
    UniValue created;
    BOOST_REQUIRE(LabCall("lab.repository_create", repo, created, err));
    const std::string repository = created["repository_id"].get_str();

    UniValue commit(UniValue::VOBJ);
    commit.pushKV("tree", "independent source\n");
    UniValue snap(UniValue::VOBJ);
    snap.pushKV("repository_id", repository);
    snap.pushKV("commit", commit);
    snap.pushKV("dependency_lock_id", std::string(96, 'd'));
    snap.pushKV("history_required", false);
    snap.pushKV("idempotency_key", "idempotency-disk-snap");
    snap.pushKV("expected_state", UniValue());
    BOOST_REQUIRE(LabCall("lab.git_snapshot", snap, created, err));

    UniValue exp(UniValue::VOBJ);
    exp.pushKV("repository_id", repository);
    exp.pushKV("idempotency_key", "idempotency-disk-export");
    exp.pushKV("expected_state", UniValue());
    UniValue exported;
    BOOST_REQUIRE(LabCall("lab.snapshot_export", exp, exported, err));
    BOOST_CHECK_EQUAL(exported["object_count"].getInt<int>(), 2);
    const std::string manifest = exported["manifest"].get_str();
    BOOST_CHECK(manifest.find(private_id) == std::string::npos);

    std::vector<modelnet::ExchangeObject> objects;
    BOOST_REQUIRE(modelnet::VerifyExchangeManifest(manifest, objects, err));
    int research_rows = 0;
    std::string line;
    std::string digest;
    for (const modelnet::ExchangeObject& object : objects) {
        if (object.kind != "research") continue;
        ++research_rows;
        line = object.content;
        digest = object.digest;
    }
    BOOST_CHECK_EQUAL(research_rows, 1);
    BOOST_CHECK(line.find('\n') == std::string::npos);
    BOOST_CHECK(line.rfind("v1|", 0) == 0);
    BOOST_CHECK(line.find(negative_id) != std::string::npos);
    BOOST_CHECK(line.find(private_id) == std::string::npos);
    BOOST_CHECK_EQUAL(line.substr(line.size() - 8), "|1|1|0|0");
    const std::vector<unsigned char> bytes(line.begin(), line.end());
    std::string expect;
    BOOST_REQUIRE(modelnet::GitObjectDigest("research", Span<const unsigned char>{bytes.data(), bytes.size()}, expect, err));
    BOOST_CHECK_EQUAL(digest, expect);

    modelnet::ResetLabApi();
    UniValue import(UniValue::VOBJ);
    import.pushKV("manifest", manifest);
    import.pushKV("idempotency_key", "idempotency-disk-import");
    import.pushKV("expected_state", UniValue());
    UniValue imported;
    BOOST_REQUIRE(LabCall("lab.snapshot_import", import, imported, err));

    UniValue frontier_req(UniValue::VOBJ);
    frontier_req.pushKV("objective_id", std::string(96, 'e'));
    frontier_req.pushKV("view", "RECENT");
    frontier_req.pushKV("limit", 10);
    frontier_req.pushKV("cursor", UniValue());
    UniValue frontier;
    BOOST_REQUIRE(LabCall("lab.research_frontier", frontier_req, frontier, err));
    BOOST_REQUIRE_EQUAL(frontier["record_ids"].size(), 1U);
    BOOST_CHECK_EQUAL(frontier["record_ids"][0].get_str(), negative_id);

    modelnet::ResetLabApi();
}

BOOST_AUTO_TEST_CASE(integer_train_survives_reload_and_snapshot)
{
    BOOST_REQUIRE(!m_path_root.empty());
    ::unsetenv("BTX035_LAB_DIR");
    ::unsetenv("BTX_GUARD_PORT");
    modelnet::ResetLabApi();

    TempTree dir{m_path_root / "btx035-lab-disk" / "train"};
    LabDirEnv env(dir.path);
    modelnet::ResetLabApi();

    std::string err;
    UniValue identity(UniValue::VOBJ);
    identity.pushKV("label", "train");
    identity.pushKV("recovery_key_ids", UniValue(UniValue::VARR));
    identity.pushKV("recovery_threshold", 1);
    identity.pushKV("recovery_delay_ms", "1");
    identity.pushKV("idempotency_key", "idempotency-disk-train-id");
    identity.pushKV("expected_state", UniValue());
    UniValue identified;
    BOOST_REQUIRE(LabCall("lab.identity_create", identity, identified, err));

    const std::string negative_id(96, 'a');
    const std::string private_id(96, 'c');
    const std::string topic(96, 'b');
    UniValue topics(UniValue::VARR);
    topics.push_back(topic);

    UniValue negative(UniValue::VOBJ);
    negative.pushKV("record_id", negative_id);
    negative.pushKV("topic_ids", topics);
    negative.pushKV("visibility", "NEGATIVE");
    negative.pushKV("idempotency_key", "idempotency-disk-train-neg");
    negative.pushKV("expected_state", UniValue());
    UniValue published;
    BOOST_REQUIRE(LabCall("lab.research_publish", negative, published, err));

    UniValue hidden(UniValue::VOBJ);
    hidden.pushKV("record_id", private_id);
    hidden.pushKV("topic_ids", topics);
    hidden.pushKV("visibility", "PRIVATE");
    hidden.pushKV("idempotency_key", "idempotency-disk-train-priv");
    hidden.pushKV("expected_state", UniValue());
    BOOST_REQUIRE(LabCall("lab.research_publish", hidden, published, err));

    UniValue repo(UniValue::VOBJ);
    repo.pushKV("label", "train-source");
    repo.pushKV("nonce", "train-nonce");
    repo.pushKV("object_format", "git");
    repo.pushKV("project_genesis_id", UniValue());
    repo.pushKV("default_ref", "refs/heads/main");
    repo.pushKV("idempotency_key", "idempotency-disk-train-repo");
    repo.pushKV("expected_state", UniValue());
    UniValue created;
    BOOST_REQUIRE(LabCall("lab.repository_create", repo, created, err));
    const std::string repository = created["repository_id"].get_str();

    UniValue commit(UniValue::VOBJ);
    commit.pushKV("tree", "integer source\n");
    UniValue snap(UniValue::VOBJ);
    snap.pushKV("repository_id", repository);
    snap.pushKV("commit", commit);
    snap.pushKV("dependency_lock_id", std::string(96, 'd'));
    snap.pushKV("history_required", false);
    snap.pushKV("idempotency_key", "idempotency-disk-train-snap");
    snap.pushKV("expected_state", UniValue());
    BOOST_REQUIRE(LabCall("lab.git_snapshot", snap, created, err));

    const std::string intent(96, '1');
    UniValue prepare(UniValue::VOBJ);
    prepare.pushKV("intent_id", intent);
    prepare.pushKV("idempotency_key", "idempotency-disk-train-prep");
    prepare.pushKV("expected_state", UniValue());
    UniValue prepared;
    BOOST_REQUIRE(LabCall("lab.action_prepare", prepare, prepared, err));

    UniValue exec(UniValue::VOBJ);
    exec.pushKV("input_capsule_id", std::string(96, '2'));
    exec.pushKV("recipe_id", "INTEGER_LINEAR");
    exec.pushKV("runtime_binding_id", std::string(96, '3'));
    exec.pushKV("research_grant_id", std::string(96, '4'));
    exec.pushKV("local_capability_grant_id", intent);
    exec.pushKV("idempotency_key", "idempotency-disk-train-exec");
    exec.pushKV("expected_state", UniValue());
    UniValue ran;
    BOOST_REQUIRE(LabCall("lab.research_execute", exec, ran, err));
    BOOST_REQUIRE_EQUAL(ran["state"].get_str(), "COMPLETE");
    const std::string trajectory = ran["operation_id"].get_str();
    BOOST_REQUIRE_EQUAL(trajectory.size(), 96U);

    std::ifstream input(PathToString(dir.path / "lab.json"));
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string body = buffer.str();
    UniValue disk;
    BOOST_REQUIRE(disk.read(body));
    BOOST_CHECK(body.find("identity_sk") == std::string::npos);
    BOOST_REQUIRE(disk["automatic_spend_atoms"].isStr());
    BOOST_CHECK_EQUAL(disk["automatic_spend_atoms"].get_str(), "0");
    BOOST_REQUIRE(disk["trains"].isObject());
    BOOST_REQUIRE(disk["trains"].exists(trajectory));
    const UniValue& saved = disk["trains"][trajectory];
    BOOST_CHECK_EQUAL(saved["weight"].get_str(), "2");
    BOOST_CHECK_EQUAL(saved["bias"].get_str(), "0");
    BOOST_CHECK_EQUAL(saved["momentum_w"].get_str(), "0");
    BOOST_CHECK_EQUAL(saved["momentum_b"].get_str(), "0");
    BOOST_CHECK_EQUAL(saved["cursor"].get_str(), "8");
    BOOST_CHECK_EQUAL(saved["step"].get_str(), "8");
    const std::string state_hash = saved["state_hash"].get_str();
    BOOST_CHECK_EQUAL(state_hash.size(), 96U);

    UniValue exp(UniValue::VOBJ);
    exp.pushKV("repository_id", repository);
    exp.pushKV("idempotency_key", "idempotency-disk-train-export");
    exp.pushKV("expected_state", UniValue());
    UniValue exported;
    BOOST_REQUIRE(LabCall("lab.snapshot_export", exp, exported, err));
    BOOST_CHECK_EQUAL(exported["object_count"].getInt<int>(), 3);
    const std::string manifest = exported["manifest"].get_str();
    BOOST_CHECK(manifest.find(private_id) == std::string::npos);
    BOOST_CHECK(manifest.find(negative_id) != std::string::npos);

    std::vector<modelnet::ExchangeObject> objects;
    BOOST_REQUIRE(modelnet::VerifyExchangeManifest(manifest, objects, err));
    int research_rows = 0;
    int train_rows = 0;
    std::string train_line;
    std::string train_digest;
    for (const modelnet::ExchangeObject& object : objects) {
        if (object.kind == "research") ++research_rows;
        if (object.kind != "train") continue;
        ++train_rows;
        train_line = object.content;
        train_digest = object.digest;
    }
    BOOST_CHECK_EQUAL(research_rows, 1);
    BOOST_CHECK_EQUAL(train_rows, 1);
    const std::string canonical = "v1|" + trajectory + "|2|0|0|0|8|8|" + state_hash;
    BOOST_CHECK_EQUAL(train_line, canonical);
    BOOST_CHECK(train_line.find('\n') == std::string::npos);
    BOOST_CHECK(train_line.find('\r') == std::string::npos);
    const std::vector<unsigned char> bytes(train_line.begin(), train_line.end());
    std::string expect;
    BOOST_REQUIRE(modelnet::GitObjectDigest("train", Span<const unsigned char>{bytes.data(), bytes.size()}, expect, err));
    BOOST_CHECK_EQUAL(train_digest, expect);

    modelnet::ReloadLabFromDisk();
    UniValue audit_req(UniValue::VOBJ);
    audit_req.pushKV("trajectory_id", trajectory);
    audit_req.pushKV("policy_id", std::string(96, 'e'));
    UniValue coverage;
    BOOST_REQUIRE(LabCall("lab.audit_coverage", audit_req, coverage, err));
    BOOST_CHECK(coverage["complete_path"].isTrue());

    modelnet::ResetLabApi();
    UniValue import(UniValue::VOBJ);
    import.pushKV("manifest", manifest);
    import.pushKV("idempotency_key", "idempotency-disk-train-import");
    import.pushKV("expected_state", UniValue());
    UniValue imported;
    BOOST_REQUIRE(LabCall("lab.snapshot_import", import, imported, err));
    BOOST_REQUIRE(LabCall("lab.audit_coverage", audit_req, coverage, err));
    BOOST_CHECK(coverage["complete_path"].isTrue());

    modelnet::ResetLabApi();
}

BOOST_AUTO_TEST_SUITE_END()
