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

UniValue ContributionPayload(const char* kind, const UniValue& physical, const UniValue& informed)
{
    UniValue payload(UniValue::VOBJ);
    payload.pushKV("objective_id", std::string(96, 'b'));
    payload.pushKV("kind", kind);
    payload.pushKV("source_snapshot_id", std::string(96, 'c'));
    payload.pushKV("physical_parent_ids", physical);
    payload.pushKV("informed_by", informed);
    payload.pushKV("capsule_id", UniValue());
    payload.pushKV("claim_refs", UniValue(UniValue::VARR));
    payload.pushKV("evidence_refs", UniValue(UniValue::VARR));
    payload.pushKV("summary", "local");
    return payload;
}

std::string SignAndStore(const char* record_type, const UniValue& payload, const char* idem)
{
    UniValue sign(UniValue::VOBJ);
    sign.pushKV("record_type", record_type);
    sign.pushKV("payload", payload);
    sign.pushKV("realm_id", std::string(96, 'a'));
    sign.pushKV("delegation_id", UniValue());
    sign.pushKV("expires_ms", "10");
    sign.pushKV("idempotency_key", idem);
    sign.pushKV("expected_state", UniValue());
    UniValue signed_result;
    std::string err;
    BOOST_REQUIRE(LabCall("lab.identity_sign", sign, signed_result, err));
    BOOST_REQUIRE(signed_result["envelope"].isObject());
    const std::string record_id = signed_result["envelope"]["record_id"].get_str();
    BOOST_REQUIRE_EQUAL(record_id.size(), 96U);
    UniValue import_req(UniValue::VOBJ);
    import_req.pushKV("envelope", signed_result["envelope"]);
    import_req.pushKV("publish", false);
    import_req.pushKV("idempotency_key", std::string("import-") + idem);
    import_req.pushKV("expected_state", UniValue());
    UniValue imported;
    BOOST_REQUIRE(LabCall("lab.record_import", import_req, imported, err));
    BOOST_CHECK_EQUAL(imported["record_id"].get_str(), record_id);
    BOOST_CHECK_EQUAL(imported["state"].get_str(), "STORED");
    return record_id;
}

void PublishStored(const std::string& record_id, const char* idem)
{
    UniValue topics(UniValue::VARR);
    topics.push_back(std::string(96, 'b'));
    UniValue req(UniValue::VOBJ);
    req.pushKV("record_id", record_id);
    req.pushKV("topic_ids", topics);
    req.pushKV("visibility", "PUBLIC");
    req.pushKV("idempotency_key", idem);
    req.pushKV("expected_state", UniValue());
    UniValue published;
    std::string err;
    BOOST_REQUIRE(LabCall("lab.research_publish", req, published, err));
    BOOST_CHECK_EQUAL(published["state"].get_str(), "PUBLISHED");
    BOOST_CHECK_EQUAL(published["record_id"].get_str(), record_id);
}

void ExpectLinkedContext(const std::string& later, const std::string& earlier, const std::string& announcement, const std::string& unknown)
{
    UniValue roots(UniValue::VARR);
    roots.push_back(later);
    UniValue req(UniValue::VOBJ);
    req.pushKV("root_ids", roots);
    req.pushKV("relations", UniValue(UniValue::VARR));
    req.pushKV("max_depth", 8);
    req.pushKV("max_records", 16);
    UniValue result;
    std::string err;
    BOOST_REQUIRE(LabCall("lab.research_context", req, result, err));
    const UniValue& ids = result["record_ids"];
    BOOST_REQUIRE_EQUAL(ids.size(), 2U);
    BOOST_CHECK_EQUAL(ids[0].get_str(), later);
    BOOST_CHECK_EQUAL(ids[1].get_str(), earlier);
    for (size_t i = 0; i < ids.size(); ++i) {
        BOOST_CHECK(ids[i].get_str() != announcement);
        BOOST_CHECK(ids[i].get_str() != unknown);
    }
    const UniValue& coverage = result["coverage"];
    BOOST_REQUIRE(coverage.exists("selection_is_proof"));
    BOOST_CHECK(coverage["selection_is_proof"].isFalse());
    BOOST_CHECK_EQUAL(coverage["physical_edges"].get_str(), "1");
    BOOST_CHECK_EQUAL(coverage["informed_edges"].get_str(), "0");
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

BOOST_AUTO_TEST_CASE(contribution_links_survive_reload_and_snapshot)
{
    BOOST_REQUIRE(!m_path_root.empty());
    ::unsetenv("BTX035_LAB_DIR");
    ::unsetenv("BTX_GUARD_PORT");
    modelnet::ResetLabApi();

    TempTree dir{m_path_root / "btx035-lab-disk" / "contrib"};
    LabDirEnv env(dir.path);
    modelnet::ResetLabApi();

    std::string err;
    UniValue identity(UniValue::VOBJ);
    identity.pushKV("label", "links");
    identity.pushKV("recovery_key_ids", UniValue(UniValue::VARR));
    identity.pushKV("recovery_threshold", 1);
    identity.pushKV("recovery_delay_ms", "1");
    identity.pushKV("idempotency_key", "idempotency-link-id");
    identity.pushKV("expected_state", UniValue());
    UniValue identified;
    BOOST_REQUIRE(LabCall("lab.identity_create", identity, identified, err));

    const std::string unknown(96, 'e');
    const std::string earlier = SignAndStore("ResearchContribution", ContributionPayload("NEGATIVE_RESULT", UniValue(UniValue::VARR), UniValue(UniValue::VARR)), "idempotency-link-sign-neg");
    PublishStored(earlier, "idempotency-link-pub-neg");

    UniValue physical(UniValue::VARR);
    physical.push_back(earlier);
    UniValue informed(UniValue::VARR);
    informed.push_back(unknown);
    const std::string later = SignAndStore("ResearchContribution", ContributionPayload("EXPERIMENT", physical, informed), "idempotency-link-sign-exp");
    PublishStored(later, "idempotency-link-pub-exp");

    UniValue announcement_payload(UniValue::VOBJ);
    announcement_payload.pushKV("objective_id", std::string(96, 'b'));
    announcement_payload.pushKV("base_contribution_ids", UniValue(UniValue::VARR));
    announcement_payload.pushKV("intent", "notice");
    announcement_payload.pushKV("expires_ms", "10");
    announcement_payload.pushKV("collaboration", false);
    announcement_payload.pushKV("contact_binding_id", std::string(96, 'd'));
    const std::string announcement = SignAndStore("ActivityAnnouncement", announcement_payload, "idempotency-link-sign-ann");
    PublishStored(announcement, "idempotency-link-pub-ann");

    UniValue repo(UniValue::VOBJ);
    repo.pushKV("label", "links");
    repo.pushKV("nonce", "link-nonce");
    repo.pushKV("object_format", "git");
    repo.pushKV("project_genesis_id", UniValue());
    repo.pushKV("default_ref", "refs/heads/main");
    repo.pushKV("idempotency_key", "idempotency-link-repo");
    repo.pushKV("expected_state", UniValue());
    UniValue created;
    BOOST_REQUIRE(LabCall("lab.repository_create", repo, created, err));
    const std::string repository = created["repository_id"].get_str();
    UniValue commit(UniValue::VOBJ);
    commit.pushKV("tree", "contribution source\n");
    UniValue snap(UniValue::VOBJ);
    snap.pushKV("repository_id", repository);
    snap.pushKV("commit", commit);
    snap.pushKV("dependency_lock_id", std::string(96, 'd'));
    snap.pushKV("history_required", false);
    snap.pushKV("idempotency_key", "idempotency-link-snap");
    snap.pushKV("expected_state", UniValue());
    BOOST_REQUIRE(LabCall("lab.git_snapshot", snap, created, err));

    std::ifstream input(PathToString(dir.path / "lab.json"));
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string body = buffer.str();
    UniValue disk;
    BOOST_REQUIRE(disk.read(body));
    BOOST_CHECK(body.find("identity_sk") == std::string::npos);
    BOOST_CHECK(!disk.exists("identity_sk"));
    BOOST_REQUIRE(disk["automatic_spend_atoms"].isStr());
    BOOST_CHECK_EQUAL(disk["automatic_spend_atoms"].get_str(), "0");
    BOOST_REQUIRE(disk["records"].exists(later));
    const std::string stored_later = disk["records"][later].get_str();
    BOOST_CHECK(stored_later.find("ResearchContribution") != std::string::npos);
    BOOST_CHECK(stored_later.find("idempotency_key") == std::string::npos);
    BOOST_CHECK(disk["records"][announcement].get_str().find("ActivityAnnouncement") != std::string::npos);

    int negative_rows = 0;
    int later_rows = 0;
    int announcement_rows = 0;
    const UniValue& saved_rows = disk["observations"];
    BOOST_REQUIRE(saved_rows.isArray());
    for (size_t i = 0; i < saved_rows.size(); ++i) {
        const UniValue& row = saved_rows[i];
        const std::string id = row["record_id"].get_str();
        if (id == announcement) ++announcement_rows;
        if (id == earlier) {
            ++negative_rows;
            BOOST_CHECK_EQUAL(row["kind"].get_str(), "NEGATIVE_RESULT");
            BOOST_CHECK(row["negative"].isTrue());
            BOOST_CHECK(row["is_private"].isFalse());
            BOOST_CHECK_EQUAL(row["physical_parent_ids"].size(), 0U);
            BOOST_CHECK_EQUAL(row["informed_by"].size(), 0U);
        }
        if (id == later) {
            ++later_rows;
            BOOST_CHECK_EQUAL(row["kind"].get_str(), "EXPERIMENT");
            BOOST_CHECK(row["negative"].isFalse());
            BOOST_REQUIRE_EQUAL(row["physical_parent_ids"].size(), 1U);
            BOOST_CHECK_EQUAL(row["physical_parent_ids"][0].get_str(), earlier);
            BOOST_REQUIRE_EQUAL(row["informed_by"].size(), 1U);
            BOOST_CHECK_EQUAL(row["informed_by"][0].get_str(), unknown);
        }
    }
    BOOST_CHECK_EQUAL(negative_rows, 1);
    BOOST_CHECK_EQUAL(later_rows, 1);
    BOOST_CHECK_EQUAL(announcement_rows, 0);

    modelnet::ReloadLabFromDisk();
    ExpectLinkedContext(later, earlier, announcement, unknown);

    UniValue exp(UniValue::VOBJ);
    exp.pushKV("repository_id", repository);
    exp.pushKV("idempotency_key", "idempotency-link-export");
    exp.pushKV("expected_state", UniValue());
    UniValue exported;
    BOOST_REQUIRE(LabCall("lab.snapshot_export", exp, exported, err));
    BOOST_CHECK_EQUAL(exported["object_count"].getInt<int>(), 5);
    const std::string manifest = exported["manifest"].get_str();
    const std::string negative_line = "v1|" + earlier + "|NEGATIVE_RESULT||";
    const std::string later_line = "v1|" + later + "|EXPERIMENT|" + earlier + "|" + unknown;
    BOOST_CHECK(manifest.find(negative_line) != std::string::npos);
    BOOST_CHECK(manifest.find(later_line) != std::string::npos);
    BOOST_CHECK(manifest.find(announcement) == std::string::npos);

    std::vector<modelnet::ExchangeObject> objects;
    BOOST_REQUIRE(modelnet::VerifyExchangeManifest(manifest, objects, err));
    int research_rows = 0;
    int lineage_rows = 0;
    for (const modelnet::ExchangeObject& object : objects) {
        if (object.kind == "research") {
            ++research_rows;
            BOOST_CHECK(object.content.find("NEGATIVE_RESULT") == std::string::npos);
            BOOST_CHECK(object.content.find('\n') == std::string::npos);
        }
        if (object.kind != "lineage") continue;
        ++lineage_rows;
        BOOST_CHECK(object.content.find(' ') == std::string::npos);
        BOOST_CHECK(object.content.find('\n') == std::string::npos);
        BOOST_CHECK(object.content.find('\r') == std::string::npos);
        const std::vector<unsigned char> bytes(object.content.begin(), object.content.end());
        std::string digest;
        BOOST_REQUIRE(modelnet::GitObjectDigest("lineage", Span<const unsigned char>{bytes.data(), bytes.size()}, digest, err));
        BOOST_CHECK_EQUAL(object.digest, digest);
    }
    BOOST_CHECK_EQUAL(research_rows, 2);
    BOOST_CHECK_EQUAL(lineage_rows, 2);

    modelnet::ResetLabApi();
    UniValue import(UniValue::VOBJ);
    import.pushKV("manifest", manifest);
    import.pushKV("idempotency_key", "idempotency-link-import");
    import.pushKV("expected_state", UniValue());
    UniValue imported;
    BOOST_REQUIRE(LabCall("lab.snapshot_import", import, imported, err));
    ExpectLinkedContext(later, earlier, announcement, unknown);

    modelnet::ResetLabApi();
}

BOOST_AUTO_TEST_SUITE_END()
