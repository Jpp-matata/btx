// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_observer.h>

#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <fstream>
#include <string>

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

BOOST_AUTO_TEST_SUITE_END()
