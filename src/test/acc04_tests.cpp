// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc04/acc04.h>
#include <modelnet/nifdt_contract/contract.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>

namespace {

void Idle(modelnet::Acc04& eng)
{
    std::string err;
    BOOST_CHECK(!eng.launched());
    BOOST_CHECK(!eng.gpu());
    BOOST_CHECK(!eng.gpu_passed());
    BOOST_CHECK(!eng.windows_passed());
    BOOST_CHECK(!eng.broadcast());
    BOOST_CHECK(!eng.qualified());
    BOOST_CHECK(!eng.release_qualified());
    BOOST_CHECK(!eng.hardware_qualified());
    BOOST_CHECK(!eng.advertised());
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.physical_hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
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
    const char* refuse;
    const char* code;
};

const KnownRow kKnown[] = {
    {"ADM-003", "kill", "ADVISORY"},
    {"RUN-001", nullptr, nullptr},
    {"RUN-002", "passthrough", "SCOPE"},
    {"RUN-003", "recall", "SENT"},
    {"RUN-004", nullptr, nullptr},
    {"RSI-001", "inherit", "GRANT"},
    {"RSI-002", "skip", "REGRESSION"},
    {"RSI-003", "transfer", "GENERATION"},
    {"REALM-001", nullptr, nullptr},
    {"REALM-002", "bridge", "BRIDGE"},
    {"REALM-003", "transitive", "TRUST"},
    {"MDT-001", "monetary", "STORE"},
    {"MDT-002", "latest", "DISAGREE"},
    {"MDT-003", nullptr, nullptr},
    {"MDT-004", "rewrite", "FOREIGN"},
    {"WAN-001", "older", "ROLLBACK"},
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(acc04_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(id_text_aux_and_flag)
{
    modelnet::Acc04 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.Apply("", "", static_cast<int64_t>(0), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored(""));
    BOOST_CHECK(eng.text("").empty());

    BOOST_CHECK(!eng.Apply("NO-SUCH", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("NO-SUCH"));

    BOOST_CHECK(!eng.Apply("ADM-004", "kill", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("ADM-004"));

    BOOST_CHECK(!eng.Apply("ADM-003", "", static_cast<int64_t>(-1), false, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK(!eng.stored("ADM-003"));

    BOOST_CHECK(!eng.Apply("ADM-003", "kill", static_cast<int64_t>(-1), true, err));
    BOOST_CHECK_EQUAL(err, "BOUND");
    BOOST_CHECK(!eng.stored("ADM-003"));

    BOOST_CHECK(!eng.Apply("ADM-003", "kill", static_cast<int64_t>(101), false, err));
    BOOST_CHECK_EQUAL(err, "CAP");
    BOOST_CHECK(!eng.stored("ADM-003"));

    BOOST_CHECK(!eng.Apply("ADM-003", "kill", static_cast<int64_t>(0), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("ADM-003"));

    BOOST_CHECK(!eng.Apply("ADM-003", "kill", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ADVISORY");
    BOOST_CHECK(!eng.stored("ADM-003"));

    BOOST_CHECK(eng.Apply("ADM-003", "s1", static_cast<int64_t>(100), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK(eng.stored("ADM-003"));
    BOOST_CHECK_EQUAL(eng.text("ADM-003"), "s1");
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));

    BOOST_CHECK(eng.Apply("RUN-004", "s1", static_cast<int64_t>(0), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.text("RUN-004"), "s1");
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(engine_private_and_exact_refusals)
{
    modelnet::Acc04 eng;
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
        if (std::string(name) != "fp8" && std::string(name) != "fp4") {
            BOOST_CHECK(!modelnet::NifDtEngineLinked(name));
        }
        BOOST_CHECK(!eng.Apply("RUN-001", name, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
        BOOST_CHECK_EQUAL(err, "ENGINE_ABSENT");
        BOOST_CHECK(!eng.stored("RUN-001"));
        BOOST_CHECK(!eng.gpu());
        BOOST_CHECK(!eng.gpu_passed());
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    }

    BOOST_CHECK(!eng.Apply("RUN-001", "private trajectory", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK_EQUAL(err, "PRIVATE_DATA");
    BOOST_CHECK(!eng.stored("RUN-001"));

    BOOST_CHECK(!eng.Apply("ADM-003", "kill private", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("ADM-003"));

    for (const KnownRow& row : kKnown) {
        if (row.refuse == nullptr) continue;
        BOOST_CHECK(!eng.Apply(row.id, row.refuse, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, row.code);
        BOOST_CHECK(!eng.stored(row.id));
        BOOST_CHECK(!eng.Apply(row.id, row.refuse, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, row.code);
        BOOST_CHECK(!eng.stored(row.id));
        if (std::string(row.id) == "RSI-003") {
            BOOST_CHECK(!eng.qualified());
            BOOST_CHECK(!eng.release_qualified());
            BOOST_CHECK(!eng.hardware_qualified());
            BOOST_CHECK(!eng.launched());
            BOOST_CHECK(!eng.gpu());
            BOOST_CHECK(!eng.gpu_passed());
            BOOST_CHECK(!eng.windows_passed());
            BOOST_CHECK(!eng.broadcast());
            BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
        }
    }
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(every_known_id_release_stays_unqualified)
{
    modelnet::Acc04 eng;
    std::string err = "stale";

    for (const KnownRow& row : kKnown) {
        if (row.refuse != nullptr) {
            BOOST_CHECK(!eng.Apply(row.id, row.refuse, static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, row.code);
            BOOST_CHECK(!eng.stored(row.id));
            if (std::string(row.id) == "RSI-003") {
                BOOST_CHECK(!eng.qualified());
            }
        }
        BOOST_CHECK(eng.Apply(row.id, "s1", static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK(eng.stored(row.id));
        BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
        BOOST_CHECK(!eng.Apply(row.id, "s1", static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
        BOOST_CHECK_EQUAL(err, "REPLAY");
        BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
        if (row.refuse != nullptr) {
            BOOST_CHECK(!eng.Apply(row.id, row.refuse, static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, row.code);
            BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
        }
        BOOST_CHECK(!eng.Apply(row.id, "s2", static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, "UNCHANGED");
        BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
        BOOST_CHECK(!eng.qualified());
        BOOST_CHECK(!eng.broadcast());
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    }

    BOOST_CHECK(!eng.Release(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK_EQUAL(err, "UNQUALIFIED");
    BOOST_CHECK(!eng.Spend(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_AUTOMATIC_SPEND);
    BOOST_CHECK_EQUAL(err, "0");
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
    BOOST_CHECK(eng.stored("RSI-003"));
    BOOST_CHECK_EQUAL(eng.text("RSI-003"), "s1");
    BOOST_CHECK(!eng.stored("NO-SUCH"));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(hosts_stay_one)
{
    modelnet::Acc04 eng;
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
    BOOST_CHECK(!eng.gpu_passed());
    BOOST_CHECK(!eng.windows_passed());
    Idle(eng);
}

BOOST_AUTO_TEST_SUITE_END()
