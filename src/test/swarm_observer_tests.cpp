// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_observer.h>
#include <modelnet/swarm_close.h>
#include <modelnet/swarm_form/plane.h>
#include <modelnet/swarm_lifecycle.h>
#include <modelnet/swarm_live.h>
#include <modelnet/swarm_node.h>

#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <fstream>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(swarm_observer_tests, BasicTestingSetup)

namespace {

const std::string kModel(96, 'a');
const std::string kArtifact(96, 'b');
const std::string kFamily(96, 'c');

void WriteFile(const fs::path& path, const std::string& body)
{
    std::ofstream out(path, std::ios::trunc);
    out << body;
}

UniValue ReadJson(const fs::path& path)
{
    std::ifstream in(path);
    UniValue root;
    const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    BOOST_REQUIRE(root.read(raw));
    return root;
}

} // namespace

BOOST_AUTO_TEST_CASE(provisional_lineage_matches_the_reference_digest)
{
    std::string model;
    model.reserve(96);
    for (int i = 0; i < 48; ++i) model += "ab";
    BOOST_CHECK_EQUAL(modelnet::SwarmProvisionalLineage(model),
                      "3959194cda24608705679e0148d84adc6b4f96ce54a3c9457605cac189c830eb86d795c3bc7d188ec86b3f425c9c5306");
}

BOOST_AUTO_TEST_CASE(verified_catalog_subscribes_an_observer_and_does_not_spend)
{
    const fs::path dir = m_args.GetDataDirBase() / "swarm_observer";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    WriteFile(dir / "catalog.json",
              "{\"models\":[{\"artifact_id\":\"" + kArtifact + "\",\"model_id\":\"" + kModel +
                  "\",\"bytes_verified\":true,\"incomplete\":false}]}");
    std::string err;
    BOOST_REQUIRE(modelnet::SwarmObserveCatalog(dir, err));
    std::ifstream in(dir / "swarm" / "observers.json");
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    UniValue root;
    BOOST_REQUIRE(root.read(raw));
    BOOST_CHECK_EQUAL(root["automatic_spend_atoms"].get_str(), "0");
    BOOST_CHECK(!root["payment_authority"].get_bool());
    const UniValue& row = root["subscriptions"][0];
    BOOST_CHECK_EQUAL(row["role"].get_str(), "observer");
    BOOST_CHECK(!row["idle_compute_authorized"].get_bool());
    BOOST_CHECK(!row["private_data_training"].get_bool());
    BOOST_CHECK_EQUAL(row["visibility"].get_str(), "private");
    BOOST_CHECK_EQUAL(row["lineage_assurance"].get_str(), "PROVISIONAL_NOT_AUTHORITATIVE");
    BOOST_CHECK_EQUAL(row["lineage_id"].get_str(), modelnet::SwarmProvisionalLineage(kModel));
    BOOST_CHECK(modelnet::SwarmObserveCatalog(dir, err));
}

BOOST_AUTO_TEST_CASE(incomplete_or_unverified_models_do_not_subscribe)
{
    const fs::path dir = m_args.GetDataDirBase() / "swarm_observer_skip";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    WriteFile(dir / "catalog.json",
              "{\"models\":[{\"artifact_id\":\"" + kArtifact + "\",\"model_id\":\"" + kModel +
                  "\",\"bytes_verified\":true,\"incomplete\":true},"
                  "{\"artifact_id\":\"" +
                  kFamily + "\",\"model_id\":\"" + kModel + "\",\"bytes_verified\":false,\"incomplete\":false}]}");
    std::string err;
    BOOST_REQUIRE(modelnet::SwarmObserveCatalog(dir, err));
    BOOST_CHECK(!fs::exists(dir / "swarm" / "observers.json"));
}

BOOST_AUTO_TEST_CASE(a_local_mapping_publishes_only_that_lineage)
{
    const fs::path dir = m_args.GetDataDirBase() / "swarm_observer_map";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "swarm");
    WriteFile(dir / "catalog.json",
              "{\"models\":[{\"artifact_id\":\"" + kArtifact + "\",\"model_id\":\"" + kModel +
                  "\",\"bytes_verified\":true,\"incomplete\":false}]}");
    WriteFile(dir / "swarm" / "lineage-mappings.json",
              "{\"" + kArtifact + "\":{\"lineage_id\":\"" + kFamily + "\",\"visibility\":\"public\"}}");
    std::string err;
    BOOST_REQUIRE(modelnet::SwarmObserveCatalog(dir, err));
    std::ifstream in(dir / "swarm" / "observers.json");
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    UniValue root;
    BOOST_REQUIRE(root.read(raw));
    BOOST_CHECK_EQUAL(root["subscriptions"][0]["lineage_id"].get_str(), kFamily);
    BOOST_CHECK_EQUAL(root["subscriptions"][0]["lineage_assurance"].get_str(), "VERIFIED_MAPPING");
    BOOST_CHECK_EQUAL(root["subscriptions"][0]["visibility"].get_str(), "public");
}

BOOST_AUTO_TEST_CASE(catalog_views_share_identity_visibility_and_invalidation)
{
    const fs::path dir = m_path_root / "catalog_views";
    fs::create_directories(dir / "swarm");
    const fs::path catalog = dir / "catalog.json";
    const fs::path mappings = dir / "swarm" / "lineage-mappings.json";
    const fs::path observers = dir / "swarm" / "observers.json";
    const fs::path contribution = dir / "swarm" / "contribution.json";
    const std::string eligible = "{\"artifact_id\":\"" + kArtifact + "\",\"model_id\":\"" + kModel +
        "\",\"bytes_verified\":true,\"incomplete\":false,\"visibility\":\"public\"}";
    const auto put_model = [&](const std::string& row) { WriteFile(catalog, "{\"models\":[" + row + "]}"); };
    std::string err;
    const auto refresh = [&] {
        BOOST_REQUIRE_MESSAGE(modelnet::SwarmObserveCatalog(dir, err), err);
        BOOST_REQUIRE_MESSAGE(modelnet::SwarmIntegrateCatalog(dir, err), err);
    };
    const auto check_identity = [&](const std::string& lineage, bool is_public, const std::string& assurance) {
        refresh();
        const UniValue observed = ReadJson(observers)["subscriptions"];
        const UniValue joined = ReadJson(contribution)["models"];
        BOOST_REQUIRE_EQUAL(observed.size(), 1U);
        BOOST_REQUIRE_EQUAL(joined.size(), 1U);
        BOOST_CHECK_EQUAL(observed[0]["lineage_id"].get_str(), lineage);
        BOOST_CHECK_EQUAL(joined[0]["lineage_id"].get_str(), lineage);
        BOOST_CHECK_EQUAL(joined[0]["model_id"].get_str(), kModel);
        BOOST_CHECK_EQUAL(joined[0]["economy_id"].get_str(), modelnet::SwarmEconomyIdentity(lineage));
        BOOST_CHECK_EQUAL(observed[0]["lineage_assurance"].get_str(), assurance);
        BOOST_CHECK_EQUAL(joined[0]["lineage_assurance"].get_str(), assurance);
        BOOST_CHECK_EQUAL(observed[0]["visibility"].get_str(), is_public ? "public" : "private");
        BOOST_CHECK_EQUAL(joined[0]["seed"].get_bool(), is_public);
        BOOST_CHECK_EQUAL(joined[0]["private_acquire"].get_bool(), !is_public);
        BOOST_CHECK(!joined[0]["kv_share"].get_bool());
        BOOST_CHECK_EQUAL(joined[0]["checkpoint_id"].get_str(), "");
        BOOST_CHECK_EQUAL(joined[0]["recipe"].get_str(), "");
    };
    put_model(eligible);
    check_identity(modelnet::SwarmProvisionalLineage(kModel), false, "PROVISIONAL_NOT_AUTHORITATIVE");
    WriteFile(mappings, "{\"" + kArtifact + "\":{\"lineage_id\":\"" + kFamily + "\",\"visibility\":\"public\"}}");
    check_identity(kFamily, true, "VERIFIED_MAPPING");
    WriteFile(mappings, "{\"" + kArtifact + "\":{\"lineage_id\":\"" + kFamily + "\",\"visibility\":\"private\"}}");
    check_identity(kFamily, false, "VERIFIED_MAPPING");
    fs::remove(mappings);
    check_identity(modelnet::SwarmProvisionalLineage(kModel), false, "PROVISIONAL_NOT_AUTHORITATIVE");

    // Each transition starts from an existing subscription, including re-add.
    for (const char* flags : {"\"bytes_verified\":false,\"incomplete\":false",
                                     "\"bytes_verified\":true,\"incomplete\":true",
                                     "\"bytes_verified\":true", // completeness unknown
                                     "\"bytes_verified\":true,\"incomplete\":\"false\""}) {
        put_model("{\"artifact_id\":\"" + kArtifact + "\",\"model_id\":\"" + kModel + "\"," + flags + "}");
        refresh();
        BOOST_CHECK(ReadJson(observers)["subscriptions"].empty());
        BOOST_CHECK(ReadJson(contribution)["models"].empty());
        put_model(eligible);
        refresh();
        BOOST_REQUIRE_EQUAL(ReadJson(observers)["subscriptions"].size(), 1U);
    }
    put_model("");
    refresh();
    BOOST_CHECK(ReadJson(observers)["subscriptions"].empty());
    BOOST_CHECK(ReadJson(contribution)["models"].empty());
    put_model(eligible);
    refresh();
    fs::remove(catalog);
    refresh();
    BOOST_CHECK(ReadJson(observers)["subscriptions"].empty());
    BOOST_CHECK(ReadJson(contribution)["models"].empty());

    // Unusable input preserves the last valid snapshot, reports failure, and
    // returns no partially parsed catalog to a caller.
    put_model(eligible);
    refresh();
    const std::string before_observer = ReadJson(observers).write();
    const std::string before_contribution = ReadJson(contribution).write();
    for (const std::string& bad : std::vector<std::string>{"{", "{\"models\":[" + eligible + "," + eligible + "]}"}) {
        WriteFile(catalog, bad);
        modelnet::SwarmCatalog entries;
        BOOST_CHECK(!modelnet::SwarmReadCatalog(dir, entries, err));
        BOOST_CHECK(entries.empty());
        BOOST_CHECK(!modelnet::SwarmObserveCatalog(dir, err));
        BOOST_CHECK(!modelnet::SwarmIntegrateCatalog(dir, err));
        BOOST_CHECK_EQUAL(ReadJson(observers).write(), before_observer);
        BOOST_CHECK_EQUAL(ReadJson(contribution).write(), before_contribution);
    }
    put_model(eligible);
    WriteFile(mappings, "{\"" + kArtifact + "\":{\"lineage_id\":\"" + kFamily + "\",\"visibility\":false}}");
    BOOST_CHECK(!modelnet::SwarmObserveCatalog(dir, err));
    BOOST_CHECK(!modelnet::SwarmIntegrateCatalog(dir, err));
}

BOOST_AUTO_TEST_CASE(unchanged_generation_does_not_rewrite_derived_files)
{
    const fs::path dir = m_path_root / "catalog_quiet";
    fs::create_directories(dir / "swarm");
    const fs::path catalog = dir / "catalog.json";
    const fs::path mappings = dir / "swarm" / "lineage-mappings.json";
    WriteFile(catalog,
              "{\"models\":[{\"artifact_id\":\"" + kArtifact + "\",\"model_id\":\"" + kModel +
                  "\",\"bytes_verified\":true,\"incomplete\":false,\"quantization\":\"full\","
                  "\"checkpoint_id\":\"ckpt\",\"recipe\":\"recipe\"}]}");
    WriteFile(mappings, "{\"" + kArtifact + "\":{\"lineage_id\":\"" + kFamily + "\",\"visibility\":\"public\"}}");
    std::string err;
    BOOST_REQUIRE(modelnet::SwarmObserveCatalog(dir, err));
    BOOST_REQUIRE(modelnet::SwarmIntegrateCatalog(dir, err));
    BOOST_REQUIRE(modelnet::SwarmRefreshLocalMesh(dir, err));
    BOOST_REQUIRE(modelnet::SwarmPublishLive(dir, err));
    BOOST_REQUIRE(modelnet::SwarmFormPlane(dir, err));
    const fs::path observers = dir / "swarm" / "observers.json";
    const fs::path contribution = dir / "swarm" / "contribution.json";
    const fs::path mesh = dir / "swarm" / "mesh-state.json";
    const fs::path networks = dir / "swarm" / "networks.json";
    const fs::path membership = dir / "swarm" / modelnet::kMembershipFile;
    const std::string membership_bytes = [&] {
        std::ifstream in(membership);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }();
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(3);
    for (const fs::path& path : {observers, contribution, mesh, networks, membership}) {
        fs::last_write_time(path, old);
    }
    const auto observers_stamp = fs::last_write_time(observers);
    const auto contribution_stamp = fs::last_write_time(contribution);
    const auto mesh_stamp = fs::last_write_time(mesh);
    const auto networks_stamp = fs::last_write_time(networks);
    const auto membership_stamp = fs::last_write_time(membership);
    BOOST_REQUIRE(modelnet::SwarmObserveCatalog(dir, err));
    BOOST_REQUIRE(modelnet::SwarmIntegrateCatalog(dir, err));
    BOOST_REQUIRE(modelnet::SwarmRefreshLocalMesh(dir, err));
    BOOST_REQUIRE(modelnet::SwarmPublishLive(dir, err));
    BOOST_REQUIRE(modelnet::SwarmFormPlane(dir, err));
    BOOST_CHECK(fs::last_write_time(observers) == observers_stamp);
    BOOST_CHECK(fs::last_write_time(contribution) == contribution_stamp);
    BOOST_CHECK(fs::last_write_time(mesh) == mesh_stamp);
    BOOST_CHECK(fs::last_write_time(networks) == networks_stamp);
    BOOST_CHECK(fs::last_write_time(membership) == membership_stamp);
    BOOST_CHECK_EQUAL(([&] {
                          std::ifstream in(membership);
                          return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                      })(),
                      membership_bytes);

    WriteFile(mappings, "{\"" + kArtifact + "\":{\"lineage_id\":\"" + kFamily + "\",\"visibility\":\"private\"}}");
    BOOST_REQUIRE(modelnet::SwarmObserveCatalog(dir, err));
    BOOST_REQUIRE(modelnet::SwarmIntegrateCatalog(dir, err));
    BOOST_REQUIRE(modelnet::SwarmPublishLive(dir, err));
    BOOST_CHECK(fs::last_write_time(observers) != observers_stamp);
    BOOST_CHECK(fs::last_write_time(contribution) != contribution_stamp);
    BOOST_CHECK_EQUAL(ReadJson(observers)["subscriptions"][0]["visibility"].get_str(), "private");
    BOOST_CHECK(!ReadJson(contribution)["models"][0]["seed"].get_bool());
    BOOST_CHECK(ReadJson(contribution)["models"][0]["private_acquire"].get_bool());

    WriteFile(catalog, "{\"models\":[]}");
    BOOST_REQUIRE(modelnet::SwarmObserveCatalog(dir, err));
    BOOST_REQUIRE(modelnet::SwarmIntegrateCatalog(dir, err));
    BOOST_REQUIRE(modelnet::SwarmPublishLive(dir, err));
    BOOST_CHECK(ReadJson(observers)["subscriptions"].empty());
    BOOST_CHECK(ReadJson(contribution)["models"].empty());
    BOOST_CHECK(ReadJson(networks)["family"]["member_artifact_ids"].empty());
}

BOOST_AUTO_TEST_SUITE_END()
