// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc06/acc06.h>
#include <modelnet/nifdt_contract/contract.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>

namespace {

void Idle(modelnet::Acc06& eng)
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
    BOOST_CHECK(!eng.gpu_passed());
    BOOST_CHECK(!eng.Spend(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_AUTOMATIC_SPEND);
    BOOST_CHECK_EQUAL(err, "0");
    BOOST_CHECK_EQUAL(eng.classification(), "SIMULATION_NOT_HARDWARE_MEASUREMENT");
}

struct KnownRow {
    const char* id;
    const char* refusal;
    const char* code;
};

const KnownRow kKnown[] = {
    {"MIG-001", "wallet", "UNTOUCHED"},
    {"MIG-002", "fabricate", "LEGACY"},
    {"MIG-003", "downgrade", "PROTOCOL"},
    {"BUILD-001", nullptr, nullptr},
    {"BUILD-002", "windows", "UNQUALIFIED"},
    {"BUILD-003", "skip", "MEASURE"},
    {"INC-001", "blocklist", "SCOPE"},
    {"INC-002", "self", "RUNBOOK"},
    {"DEV-001", nullptr, nullptr},
    {"DEV-002", "grep", "EVIDENCE"},
    {"DEV-003", "doc", "JOURNEY"},
    {"E2E-001", "assign", "AUTHORITY"},
    {"E2E-002", "altered", "MISMATCH"},
    {"E2E-003", "hook", "SAFE"},
    {"E2E-004", "one-attestor", "QUORUM"},
    {"E2E-005", "shrink", "THRESHOLD"},
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(acc06_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(id_text_aux_and_flag)
{
    modelnet::Acc06 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.Apply("", "", static_cast<int64_t>(0), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored(""));

    BOOST_CHECK(!eng.Apply("MIG-004", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("MIG-004"));

    BOOST_CHECK(!eng.Apply("BUILD-004", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("BUILD-004"));

    BOOST_CHECK(!eng.Apply("E2E-006", "wallet", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("E2E-006"));

    BOOST_CHECK(!eng.Apply("MIG-001", "", static_cast<int64_t>(-1), false, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK(!eng.stored("MIG-001"));

    BOOST_CHECK(!eng.Apply("MIG-001", "s1", static_cast<int64_t>(-1), true, err));
    BOOST_CHECK_EQUAL(err, "BOUND");
    BOOST_CHECK(!eng.stored("MIG-001"));

    BOOST_CHECK(!eng.Apply("MIG-001", "s1", static_cast<int64_t>(101), false, err));
    BOOST_CHECK_EQUAL(err, "CAP");
    BOOST_CHECK(!eng.stored("MIG-001"));

    BOOST_CHECK(!eng.Apply("MIG-001", "s1", static_cast<int64_t>(0), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("MIG-001"));

    BOOST_CHECK(!eng.Apply("BUILD-002", "windows", static_cast<int64_t>(-1), true, err));
    BOOST_CHECK_EQUAL(err, "BOUND");
    BOOST_CHECK(!eng.stored("BUILD-002"));
    BOOST_CHECK(!eng.windows_passed());

    BOOST_CHECK(!eng.Apply("BUILD-002", "windows", static_cast<int64_t>(1), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("BUILD-002"));
    BOOST_CHECK(!eng.windows_passed());

    BOOST_CHECK(eng.Apply("MIG-001", "s1", static_cast<int64_t>(100), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK(eng.stored("MIG-001"));
    BOOST_CHECK_EQUAL(eng.text("MIG-001"), "s1");
    BOOST_CHECK(eng.Apply("BUILD-001", "s1", static_cast<int64_t>(0), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.text("BUILD-001"), "s1");
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(engines_private_and_exact_refusals)
{
    modelnet::Acc06 eng;
    std::string err = "stale";
    const char* const remote[] = {
        "vllm",
        "sglang",
        "lmcache",
        "nixl",
        "mooncake",
        "rdma",
        "deepinfer",
        "dualpipe",
    };

    BOOST_CHECK(modelnet::NifDtEngineLinked("fp8"));
    BOOST_CHECK(!eng.Apply("BUILD-001", "fp8", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK_EQUAL(err, "ENGINE_ABSENT");
    BOOST_CHECK(!eng.stored("BUILD-001"));
    BOOST_CHECK(!eng.gpu());
    BOOST_CHECK(!eng.gpu_passed());

    BOOST_CHECK(modelnet::NifDtEngineLinked("fp4"));
    BOOST_CHECK(!eng.Apply("BUILD-001", "fp4", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.stored("BUILD-001"));
    BOOST_CHECK(!eng.gpu_passed());

    BOOST_CHECK(!modelnet::NifDtEngineLinked("dualpipe"));
    BOOST_CHECK(!eng.Apply("BUILD-001", "dualpipe", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.stored("BUILD-001"));

    for (const char* name : remote) {
        BOOST_CHECK(!modelnet::NifDtEngineLinked(name));
        BOOST_CHECK(!eng.Apply("BUILD-001", name, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
        BOOST_CHECK(!eng.stored("BUILD-001"));
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    }

    BOOST_CHECK(!eng.Apply("MIG-001", "private trajectory", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK_EQUAL(err, "PRIVATE_DATA");
    BOOST_CHECK(!eng.stored("MIG-001"));

    BOOST_CHECK(!eng.Apply("MIG-001", "private wallet", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("MIG-001"));

    BOOST_CHECK(!eng.Apply("MIG-001", "wallet", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "UNTOUCHED");
    BOOST_CHECK(!eng.stored("MIG-001"));

    BOOST_CHECK(!eng.Apply("MIG-002", "fabricate", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "LEGACY");
    BOOST_CHECK(!eng.stored("MIG-002"));

    BOOST_CHECK(!eng.Apply("MIG-003", "downgrade", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "PROTOCOL");
    BOOST_CHECK(!eng.stored("MIG-003"));

    BOOST_CHECK(!eng.Apply("BUILD-002", "windows", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK_EQUAL(err, "UNQUALIFIED");
    BOOST_CHECK(!eng.stored("BUILD-002"));
    BOOST_CHECK(!eng.windows_passed());
    BOOST_CHECK(!eng.gpu_passed());
    BOOST_CHECK(!eng.gpu());
    BOOST_CHECK(!eng.qualified());
    BOOST_CHECK(!eng.broadcast());
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));

    BOOST_CHECK(!eng.Apply("BUILD-002", "macos", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK(!eng.stored("BUILD-002"));
    BOOST_CHECK(!eng.windows_passed());

    BOOST_CHECK(!eng.Apply("BUILD-003", "skip", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "MEASURE");
    BOOST_CHECK(!eng.stored("BUILD-003"));

    BOOST_CHECK(!eng.Apply("INC-001", "blocklist", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "SCOPE");
    BOOST_CHECK(!eng.stored("INC-001"));

    BOOST_CHECK(!eng.Apply("INC-002", "self", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "RUNBOOK");
    BOOST_CHECK(!eng.stored("INC-002"));

    BOOST_CHECK(!eng.Apply("DEV-002", "grep", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "EVIDENCE");
    BOOST_CHECK(!eng.stored("DEV-002"));

    BOOST_CHECK(!eng.Apply("DEV-003", "doc", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "JOURNEY");
    BOOST_CHECK(!eng.stored("DEV-003"));

    BOOST_CHECK(!eng.Apply("E2E-001", "assign", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "AUTHORITY");
    BOOST_CHECK(!eng.stored("E2E-001"));

    BOOST_CHECK(!eng.Apply("E2E-002", "altered", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "MISMATCH");
    BOOST_CHECK(!eng.stored("E2E-002"));

    BOOST_CHECK(!eng.Apply("E2E-003", "hook", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "SAFE");
    BOOST_CHECK(!eng.stored("E2E-003"));

    BOOST_CHECK(!eng.Apply("E2E-004", "one-attestor", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "QUORUM");
    BOOST_CHECK(!eng.stored("E2E-004"));

    BOOST_CHECK(!eng.Apply("E2E-005", "shrink", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "THRESHOLD");
    BOOST_CHECK(!eng.stored("E2E-005"));

    BOOST_CHECK(!eng.Spend(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_AUTOMATIC_SPEND);
    BOOST_CHECK_EQUAL(err, "0");
    BOOST_CHECK(!eng.broadcast());
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    BOOST_CHECK(!eng.windows_passed());
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(every_known_id_release_stays_unqualified)
{
    modelnet::Acc06 eng;
    std::string err = "stale";

    for (const KnownRow& row : kKnown) {
        if (row.refusal != nullptr) {
            BOOST_CHECK(!eng.Apply(row.id, row.refusal, static_cast<int64_t>(1), true, err));
            if (std::string(row.id) == "BUILD-002") {
                BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
                BOOST_CHECK(!eng.windows_passed());
            }
            BOOST_CHECK_EQUAL(err, row.code);
            BOOST_CHECK(!eng.stored(row.id));
        }
        if (std::string(row.id) == "BUILD-002") {
            BOOST_CHECK(!eng.Apply(row.id, "macos", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
            BOOST_CHECK_EQUAL(err, "UNQUALIFIED");
            BOOST_CHECK(!eng.stored(row.id));
            BOOST_CHECK(!eng.windows_passed());
        }
        BOOST_CHECK(eng.Apply(row.id, "s1", static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK(eng.stored(row.id));
        BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
        BOOST_CHECK_EQUAL(eng.classification(), "SIMULATION_NOT_HARDWARE_MEASUREMENT");
        BOOST_CHECK(!eng.Apply(row.id, "s1", static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
        BOOST_CHECK_EQUAL(err, "REPLAY");
        BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
        BOOST_CHECK(!eng.Apply(row.id, "s2", static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, "UNCHANGED");
        BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
    }

    BOOST_CHECK(!eng.Apply("BUILD-002", "windows", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK_EQUAL(eng.text("BUILD-002"), "s1");
    BOOST_CHECK(eng.stored("BUILD-002"));
    BOOST_CHECK(!eng.windows_passed());
    BOOST_CHECK(!eng.gpu_passed());

    BOOST_CHECK(!eng.Apply("MIG-001", "wallet", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "UNTOUCHED");
    BOOST_CHECK_EQUAL(eng.text("MIG-001"), "s1");

    BOOST_CHECK(!eng.Apply("DEV-001", "fp8", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK_EQUAL(eng.text("DEV-001"), "s1");

    BOOST_CHECK(!eng.Apply("DEV-001", "private trajectory", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK_EQUAL(eng.text("DEV-001"), "s1");

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
    BOOST_CHECK(eng.stored("MIG-001"));
    BOOST_CHECK_EQUAL(eng.text("E2E-005"), "s1");
    BOOST_CHECK(eng.stored("BUILD-001"));
    BOOST_CHECK(eng.stored("DEV-001"));
    BOOST_CHECK(!eng.stored("MIG-004"));
    BOOST_CHECK(!eng.stored("E2E-006"));
    BOOST_CHECK(!eng.Spend(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_AUTOMATIC_SPEND);
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(hosts_stay_one)
{
    modelnet::Acc06 eng;
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
    BOOST_CHECK(!eng.windows_passed());
    BOOST_CHECK(!eng.gpu_passed());
    Idle(eng);
}

BOOST_AUTO_TEST_SUITE_END()
