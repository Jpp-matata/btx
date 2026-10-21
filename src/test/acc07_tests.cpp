// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc07/acc07.h>
#include <modelnet/nifdt_contract/contract.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>

namespace {

void Idle(modelnet::Acc07& eng)
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

const char* const kEngines[] = {
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

struct Refusal {
    const char* id;
    const char* text;
    const char* code;
};

const Refusal kRefusals[] = {
    {"E2E-006", "backup", "ROLLBACK"},
    {"QA-001", "skip", "FAIL"},
    {"QA-002", "native", "PACKAGE"},
    {"QA-003", "growth", "CLAIM"},
    {"REV-001", "1.0.0", "REVISION"},
    {"REV-002", "signed-release", "UNSIGNED"},
    {"REV-004", "cycle", "CYCLE"},
    {"REV-005", "empty-keys", "PENDING"},
    {"REV-008", "early", "BATCH"},
    {"REV-009", "shell", "ACTION"},
    {"REV-010", "default", "PROFILE"},
    {"REV-011", "weaker", "STRICT"},
    {"REV-013", "late", "PLAN"},
    {"REV-014", "shared", "TOKEN"},
    {"REV-015", "score", "BINDING"},
};

const char* const kStored[] = {
    "E2E-006",
    "QA-001",
    "QA-002",
    "QA-003",
    "REV-001",
    "REV-002",
    "REV-003",
    "REV-004",
    "REV-005",
    "REV-006",
    "REV-007",
    "REV-008",
    "REV-009",
    "REV-010",
    "REV-011",
    "REV-012",
    "REV-013",
    "REV-014",
    "REV-015",
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(acc07_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(id_text_aux_and_flag)
{
    modelnet::Acc07 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.Apply("", "", static_cast<int64_t>(0), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored(""));

    BOOST_CHECK(!eng.Apply("REV-000", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("REV-000"));

    BOOST_CHECK(!eng.Apply("E2E-001", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("E2E-001"));

    BOOST_CHECK(!eng.Apply("QA-004", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("QA-004"));

    BOOST_CHECK(!eng.Apply("REV-003", "", static_cast<int64_t>(-1), false, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK(!eng.stored("REV-003"));

    BOOST_CHECK(!eng.Apply("REV-003", "s1", static_cast<int64_t>(-1), true, err));
    BOOST_CHECK_EQUAL(err, "BOUND");
    BOOST_CHECK(!eng.stored("REV-003"));

    BOOST_CHECK(!eng.Apply("REV-003", "s1", static_cast<int64_t>(101), false, err));
    BOOST_CHECK_EQUAL(err, "CAP");
    BOOST_CHECK(!eng.stored("REV-003"));

    BOOST_CHECK(!eng.Apply("REV-003", "s1", static_cast<int64_t>(0), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("REV-003"));

    BOOST_CHECK(eng.Apply("REV-003", "s1", static_cast<int64_t>(0), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK(eng.stored("REV-003"));
    BOOST_CHECK_EQUAL(eng.text("REV-003"), "s1");

    BOOST_CHECK(eng.Apply("REV-012", "s1", static_cast<int64_t>(100), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.text("REV-012"), "s1");
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(rev016_unqualified_after_gates)
{
    modelnet::Acc07 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.Apply("REV-016", "", static_cast<int64_t>(-1), false, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK(!eng.stored("REV-016"));

    BOOST_CHECK(!eng.Apply("REV-016", "s1", static_cast<int64_t>(-1), true, err));
    BOOST_CHECK_EQUAL(err, "BOUND");
    BOOST_CHECK(!eng.stored("REV-016"));

    BOOST_CHECK(!eng.Apply("REV-016", "s1", static_cast<int64_t>(101), false, err));
    BOOST_CHECK_EQUAL(err, "CAP");
    BOOST_CHECK(!eng.stored("REV-016"));

    BOOST_CHECK(!eng.Apply("REV-016", "s1", static_cast<int64_t>(1), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("REV-016"));

    BOOST_CHECK(!eng.Apply("REV-016", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK_EQUAL(err, "UNQUALIFIED");
    BOOST_CHECK(!eng.stored("REV-016"));
    BOOST_CHECK(!eng.qualified());
    BOOST_CHECK(!eng.release_qualified());

    BOOST_CHECK(!eng.Apply("REV-016", "s1", static_cast<int64_t>(0), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK(!eng.stored("REV-016"));

    BOOST_CHECK(!eng.Apply("REV-016", "s1", static_cast<int64_t>(100), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK(!eng.stored("REV-016"));
    BOOST_CHECK_EQUAL(eng.text("REV-016"), "");
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    BOOST_CHECK(!eng.broadcast());
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(engine_private_and_exact_refusals)
{
    modelnet::Acc07 eng;
    std::string err = "stale";

    BOOST_CHECK(modelnet::NifDtEngineLinked("fp8"));
    BOOST_CHECK(modelnet::NifDtEngineLinked("fp4"));
    for (const char* name : kEngines) {
        BOOST_CHECK(!eng.Apply("QA-001", name, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
        BOOST_CHECK_EQUAL(err, "ENGINE_ABSENT");
        BOOST_CHECK(!eng.stored("QA-001"));
        BOOST_CHECK(!eng.gpu());
        BOOST_CHECK(!eng.gpu_passed());
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    }

    BOOST_CHECK(!modelnet::NifDtEngineLinked("deepep"));
    BOOST_CHECK(eng.Apply("REV-006", "deepep", static_cast<int64_t>(1), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK(eng.stored("REV-006"));
    BOOST_CHECK_EQUAL(eng.text("REV-006"), "deepep");

    BOOST_CHECK(!eng.Apply("QA-002", "private trajectory", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK_EQUAL(err, "PRIVATE_DATA");
    BOOST_CHECK(!eng.stored("QA-002"));

    BOOST_CHECK(!eng.Apply("E2E-006", "keep private backup", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("E2E-006"));

    for (const Refusal& row : kRefusals) {
        BOOST_CHECK(!eng.Apply(row.id, row.text, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, row.code);
        BOOST_CHECK(!eng.stored(row.id));
        BOOST_CHECK(eng.Apply(row.id, "s1", static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
        BOOST_CHECK(!eng.Apply(row.id, row.text, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, row.code);
        BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
        BOOST_CHECK(!eng.Apply(row.id, "s1", static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
        BOOST_CHECK_EQUAL(err, "REPLAY");
        BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
        BOOST_CHECK(!eng.Apply(row.id, "s2", static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, "UNCHANGED");
        BOOST_CHECK_EQUAL(eng.text(row.id), "s1");
    }

    BOOST_CHECK(!eng.broadcast());
    BOOST_CHECK(!eng.windows_passed());
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(every_other_id_release_stays_unqualified)
{
    modelnet::Acc07 eng;
    std::string err = "stale";

    for (const char* id : kStored) {
        BOOST_CHECK(eng.Apply(id, "s1", static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK(eng.stored(id));
        BOOST_CHECK_EQUAL(eng.text(id), "s1");
        BOOST_CHECK(!eng.Apply(id, "s1", static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
        BOOST_CHECK_EQUAL(err, "REPLAY");
        BOOST_CHECK_EQUAL(eng.text(id), "s1");
        BOOST_CHECK(!eng.Apply(id, "other-sentence", static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, "UNCHANGED");
        BOOST_CHECK_EQUAL(eng.text(id), "s1");
    }

    BOOST_CHECK(!eng.Apply("REV-016", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK(!eng.stored("REV-016"));

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
    BOOST_CHECK(eng.stored("E2E-006"));
    BOOST_CHECK_EQUAL(eng.text("REV-015"), "s1");
    BOOST_CHECK(eng.stored("REV-003"));
    BOOST_CHECK(eng.stored("REV-006"));
    BOOST_CHECK(eng.stored("REV-007"));
    BOOST_CHECK(eng.stored("REV-012"));
    BOOST_CHECK(!eng.stored("REV-016"));
    BOOST_CHECK(!eng.Spend(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_AUTOMATIC_SPEND);
    BOOST_CHECK_EQUAL(err, "0");
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(hosts_stay_one)
{
    modelnet::Acc07 eng;
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
