// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc01/acc01.h>
#include <modelnet/nifdt_contract/contract.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>

namespace {

void Idle(modelnet::Acc01& eng)
{
    std::string err;
    BOOST_CHECK(!eng.launched());
    BOOST_CHECK(!eng.gpu());
    BOOST_CHECK(!eng.broadcast());
    BOOST_CHECK(!eng.qualified());
    BOOST_CHECK(!eng.release_qualified());
    BOOST_CHECK(!eng.hardware_qualified());
    BOOST_CHECK(!eng.advertised());
    BOOST_CHECK(!eng.gpu_passed());
    BOOST_CHECK(!eng.windows_passed());
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.physical_hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    BOOST_CHECK_EQUAL(eng.automatic_spend(), modelnet::NIFDT_AUTOMATIC_SPEND);
    BOOST_CHECK_EQUAL(eng.automatic_spend(), "0");
    BOOST_CHECK(!eng.Vendor(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK_EQUAL(err, "ENGINE_ABSENT");
    BOOST_CHECK(!eng.Release(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK_EQUAL(err, "UNQUALIFIED");
    BOOST_CHECK(!eng.Gpu(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.Spend(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_AUTOMATIC_SPEND);
    BOOST_CHECK_EQUAL(err, "0");
    BOOST_CHECK_EQUAL(eng.classification(), "SIMULATION_NOT_HARDWARE_MEASUREMENT");
}

struct KnownRow {
    const char* id;
    const char* text;
    const char* other;
};

const KnownRow kKnown[] = {
    {"CORE-001", "s1", "s2"},
    {"CORE-002", "s1", "s2"},
    {"CORE-003", "s1", "s2"},
    {"CORE-004", "s1", "s2"},
    {"CORE-005", "s1", "s2"},
    {"ARCH-001", "s1", "s2"},
    {"ARCH-002", "s1", "s2"},
    {"ARCH-003", "s1", "s2"},
    {"ARCH-004", "public_participant", "community_realm"},
    {"SEC-001", "s1", "s2"},
    {"SEC-002", "s1", "s2"},
    {"SEC-003", "s1", "s2"},
    {"SEC-004", "s1", "s2"},
    {"WIRE-001", "s1-1.1.0", "s2-1.1.0"},
    {"WIRE-002", "s1", "s2"},
    {"WIRE-003", "s1", "s2"},
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(acc01_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(id_text_aux_and_flag)
{
    modelnet::Acc01 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.Apply("", "", static_cast<int64_t>(0), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored(""));

    BOOST_CHECK(!eng.Apply("NO-SUCH", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("NO-SUCH"));

    BOOST_CHECK(!eng.Apply("CORE-006", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("CORE-006"));

    BOOST_CHECK(!eng.Apply("CORE-001", "", static_cast<int64_t>(-1), false, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK(!eng.stored("CORE-001"));

    BOOST_CHECK(!eng.Apply("CORE-001", "s1", static_cast<int64_t>(-1), true, err));
    BOOST_CHECK_EQUAL(err, "BOUND");
    BOOST_CHECK(!eng.stored("CORE-001"));

    BOOST_CHECK(!eng.Apply("CORE-001", "s1", static_cast<int64_t>(101), false, err));
    BOOST_CHECK_EQUAL(err, "CAP");
    BOOST_CHECK(!eng.stored("CORE-001"));

    BOOST_CHECK(!eng.Apply("CORE-001", "s1", static_cast<int64_t>(0), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("CORE-001"));

    BOOST_CHECK(eng.Apply("CORE-001", "s1", static_cast<int64_t>(0), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK(eng.stored("CORE-001"));
    BOOST_CHECK_EQUAL(eng.text("CORE-001"), "s1");

    BOOST_CHECK(eng.Apply("CORE-002", "s1", static_cast<int64_t>(100), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.text("CORE-002"), "s1");
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(engines_private_and_specials)
{
    modelnet::Acc01 eng;
    std::string err = "stale";
    const char* const engines[] = {
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

    BOOST_CHECK(modelnet::NifDtEngineLinked("fp8"));
    BOOST_CHECK(modelnet::NifDtEngineLinked("fp4"));
    for (const char* name : engines) {
        BOOST_CHECK(!eng.Apply("CORE-001", name, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
        BOOST_CHECK_EQUAL(err, "ENGINE_ABSENT");
        BOOST_CHECK(!eng.stored("CORE-001"));
        BOOST_CHECK(!eng.gpu());
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    }

    BOOST_CHECK(!eng.Apply("CORE-001", "private note", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK_EQUAL(err, "PRIVATE_DATA");
    BOOST_CHECK(!eng.stored("CORE-001"));

    BOOST_CHECK(!eng.Apply("CORE-001", "private_personal", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("CORE-001"));

    BOOST_CHECK(!eng.Apply("CORE-003", "private consensus", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("CORE-003"));

    BOOST_CHECK(!eng.Apply("CORE-003", "has consensus", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "CONSENSUS");
    BOOST_CHECK(!eng.stored("CORE-003"));

    BOOST_CHECK(!eng.Apply("CORE-005", "a claim", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "CLAIM");
    BOOST_CHECK(!eng.stored("CORE-005"));

    BOOST_CHECK(!eng.Apply("ARCH-001", "hardware", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK_EQUAL(err, "UNQUALIFIED");
    BOOST_CHECK(!eng.stored("ARCH-001"));
    BOOST_CHECK(!eng.hardware_qualified());

    BOOST_CHECK(!eng.Apply("ARCH-002", "assign", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "AUTHORITY");
    BOOST_CHECK(!eng.stored("ARCH-002"));

    BOOST_CHECK(!eng.Apply("ARCH-004", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "PROFILE");
    BOOST_CHECK(!eng.stored("ARCH-004"));

    BOOST_CHECK(!eng.Apply("ARCH-004", "private note", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("ARCH-004"));

    BOOST_CHECK(!eng.Apply("SEC-002", "shell", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "QUARANTINE");
    BOOST_CHECK(!eng.stored("SEC-002"));

    BOOST_CHECK(!eng.Apply("WIRE-001", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "VERSION");
    BOOST_CHECK(!eng.stored("WIRE-001"));

    BOOST_CHECK(!eng.Apply("WIRE-003", "oversize", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "LIMIT");
    BOOST_CHECK(!eng.stored("WIRE-003"));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(arch004_profiles_store)
{
    const char* const profiles[] = {
        "public_participant",
        "private_personal",
        "community_realm",
        "enterprise_ingress",
        "enterprise_site",
        "offline_site",
    };
    for (const char* name : profiles) {
        modelnet::Acc01 eng;
        std::string err = "stale";
        BOOST_CHECK(eng.Apply("ARCH-004", name, static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK(eng.stored("ARCH-004"));
        BOOST_CHECK_EQUAL(eng.text("ARCH-004"), name);
        BOOST_CHECK(!eng.Apply("ARCH-004", name, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
        BOOST_CHECK_EQUAL(err, "REPLAY");
        BOOST_CHECK_EQUAL(eng.text("ARCH-004"), name);
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
        Idle(eng);
    }
}

BOOST_AUTO_TEST_CASE(every_known_id_release_stays_unqualified)
{
    modelnet::Acc01 eng;
    std::string err = "stale";

    for (const KnownRow& row : kKnown) {
        BOOST_CHECK(eng.Apply(row.id, row.text, static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK(eng.stored(row.id));
        BOOST_CHECK_EQUAL(eng.text(row.id), row.text);
        BOOST_CHECK(!eng.Apply(row.id, row.text, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
        BOOST_CHECK_EQUAL(err, "REPLAY");
        BOOST_CHECK_EQUAL(eng.text(row.id), row.text);
        BOOST_CHECK(!eng.Apply(row.id, row.other, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, "UNCHANGED");
        BOOST_CHECK_EQUAL(eng.text(row.id), row.text);
    }

    BOOST_CHECK(!eng.Release(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK_EQUAL(err, "UNQUALIFIED");
    BOOST_CHECK(!eng.release_qualified());
    BOOST_CHECK(!eng.hardware_qualified());
    BOOST_CHECK(!eng.qualified());
    BOOST_CHECK(!eng.advertised());
    BOOST_CHECK(!eng.launched());
    BOOST_CHECK(!eng.gpu());
    BOOST_CHECK(!eng.gpu_passed());
    BOOST_CHECK(!eng.windows_passed());
    BOOST_CHECK(!eng.broadcast());
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.physical_hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    BOOST_CHECK(eng.stored("CORE-001"));
    BOOST_CHECK_EQUAL(eng.text("CORE-001"), "s1");
    BOOST_CHECK_EQUAL(eng.text("ARCH-004"), "public_participant");
    BOOST_CHECK_EQUAL(eng.text("WIRE-001"), "s1-1.1.0");
    BOOST_CHECK(!eng.stored("NO-SUCH"));
    BOOST_CHECK(!eng.Spend(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_AUTOMATIC_SPEND);
    BOOST_CHECK_EQUAL(err, "0");
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(hosts_stay_one)
{
    modelnet::Acc01 eng;
    std::string err = "stale";
    BOOST_CHECK(!eng.Hosts(static_cast<int64_t>(2), err));
    BOOST_CHECK_EQUAL(err, "HOSTS");
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.physical_hosts(), static_cast<int64_t>(1));
    BOOST_CHECK(!eng.Hosts(static_cast<int64_t>(0), err));
    BOOST_CHECK_EQUAL(err, "HOSTS");
    BOOST_CHECK(eng.Hosts(static_cast<int64_t>(1), err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    BOOST_CHECK(!eng.broadcast());
    Idle(eng);
}

BOOST_AUTO_TEST_SUITE_END()
