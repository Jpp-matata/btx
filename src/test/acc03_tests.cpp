// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc03/acc03.h>
#include <modelnet/nifdt_contract/contract.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>

namespace {

void Idle(modelnet::Acc03& eng)
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
    const char* text;
    const char* again;
    const char* blocked;
    const char* blocked_err;
};

const KnownRow kKnown[] = {
    {"SCM-003", "bounded-fetch", "other-sentence", "fetch-all", "PREFETCH"},
    {"SCM-004", "closed-set", "other-sentence", nullptr, nullptr},
    {"RES-001", "observe", "other-sentence", "assign", "AUTHORITY"},
    {"RES-002", "local-note", "other-sentence", nullptr, nullptr},
    {"RES-003", "LEADING", "CONTESTED", "other-view", "VIEW"},
    {"RES-004", "closed-grammar", "other-sentence", "parser", "PARSER"},
    {"RES-005", "local-note", "other-sentence", nullptr, nullptr},
    {"ART-001", "frozen-label", "other-sentence", "tag", "MUTABLE"},
    {"ART-002", "local-note", "other-sentence", nullptr, nullptr},
    {"ART-003", "local-note", "other-sentence", nullptr, nullptr},
    {"VER-001", "local-check", "other-sentence", "network", "FETCH"},
    {"VER-002", "rng-s1", "rng-other", "plain", "STATE"},
    {"VER-003", "held-out", "other-sentence", "sample", "GAP"},
    {"VER-004", "other-check", "other-sentence", "self", "SELF"},
    {"ADM-001", "named-set", "other-sentence", "one-root", "QUORUM"},
    {"ADM-002", "hostmodel", "importmodel", "not-a-path", "PATH"},
};

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

const char* const kViews[] = {
    "LEADING",
    "CONTESTED",
    "NEGLECTED",
    "RECENT",
    "REPRODUCED",
};

const char* const kPaths[] = {
    "hostmodel",
    "importmodel",
    "seedmodel",
    "watched",
    "demand-seed",
    "peer-follow",
    "cloud-mirror",
    "encrypted-release",
    "bundle-import",
    "package-install",
    "hosted-gateway",
    "direct-api",
    "capability",
    "local-generate",
    "model-rollout",
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(acc03_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(id_text_aux_and_flag)
{
    modelnet::Acc03 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.Apply("", "fetch-all", static_cast<int64_t>(-1), false, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored(""));

    BOOST_CHECK(!eng.Apply("NO-SUCH", "observe", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("NO-SUCH"));

    BOOST_CHECK(!eng.Apply("SCM-001", "bounded-fetch", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("SCM-001"));

    BOOST_CHECK(!eng.Apply("SCM-003", "", static_cast<int64_t>(-1), false, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK(!eng.stored("SCM-003"));

    BOOST_CHECK(!eng.Apply("SCM-003", "bounded-fetch", static_cast<int64_t>(-1), true, err));
    BOOST_CHECK_EQUAL(err, "BOUND");
    BOOST_CHECK(!eng.stored("SCM-003"));

    BOOST_CHECK(!eng.Apply("SCM-003", "bounded-fetch", static_cast<int64_t>(101), false, err));
    BOOST_CHECK_EQUAL(err, "CAP");
    BOOST_CHECK(!eng.stored("SCM-003"));

    BOOST_CHECK(!eng.Apply("SCM-003", "bounded-fetch", static_cast<int64_t>(0), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("SCM-003"));

    BOOST_CHECK(eng.Apply("SCM-003", "bounded-fetch", static_cast<int64_t>(100), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK(eng.stored("SCM-003"));
    BOOST_CHECK_EQUAL(eng.text("SCM-003"), "bounded-fetch");
    BOOST_CHECK(eng.Apply("SCM-004", "closed-set", static_cast<int64_t>(0), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.text("SCM-004"), "closed-set");
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(exact_engine_and_private)
{
    modelnet::Acc03 eng;
    std::string err = "stale";

    BOOST_CHECK(modelnet::NifDtEngineLinked("fp8"));
    BOOST_CHECK(modelnet::NifDtEngineLinked("fp4"));
    for (const char* name : kEngines) {
        BOOST_CHECK(!eng.Apply("SCM-004", name, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
        BOOST_CHECK_EQUAL(err, "ENGINE_ABSENT");
        BOOST_CHECK(!eng.stored("SCM-004"));
        BOOST_CHECK(!eng.gpu());
        BOOST_CHECK(!eng.gpu_passed());
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    }

    BOOST_CHECK(!eng.Apply("SCM-004", "private trajectory", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK_EQUAL(err, "PRIVATE_DATA");
    BOOST_CHECK(!eng.stored("SCM-004"));

    BOOST_CHECK(!eng.Apply("SCM-003", "vllm", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.Apply("SCM-003", "private fetch-all", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("SCM-003"));

    BOOST_CHECK(!modelnet::NifDtEngineLinked("deepep"));
    BOOST_CHECK(eng.Apply("SCM-004", "deepep", static_cast<int64_t>(1), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.text("SCM-004"), "deepep");
    BOOST_CHECK(!eng.Apply("SCM-004", "dualpipev", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "UNCHANGED");
    BOOST_CHECK_EQUAL(eng.text("SCM-004"), "deepep");
    BOOST_CHECK(!eng.broadcast());
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(id_rules_before_store)
{
    modelnet::Acc03 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.Apply("SCM-003", "fetch-all", static_cast<int64_t>(1), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.Apply("SCM-003", "fetch-all", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "PREFETCH");
    BOOST_CHECK(!eng.Apply("SCM-003", "fetch-all", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "PREFETCH");
    BOOST_CHECK(!eng.stored("SCM-003"));

    BOOST_CHECK(!eng.Apply("RES-001", "assign", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "AUTHORITY");
    BOOST_CHECK(!eng.stored("RES-001"));

    BOOST_CHECK(!eng.Apply("RES-003", "recent", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "VIEW");
    BOOST_CHECK(!eng.stored("RES-003"));

    BOOST_CHECK(!eng.Apply("RES-004", "parser", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "PARSER");
    BOOST_CHECK(!eng.stored("RES-004"));

    BOOST_CHECK(!eng.Apply("ART-001", "tag", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "MUTABLE");
    BOOST_CHECK(!eng.stored("ART-001"));

    BOOST_CHECK(!eng.Apply("VER-001", "network", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "FETCH");
    BOOST_CHECK(!eng.stored("VER-001"));

    BOOST_CHECK(!eng.Apply("VER-002", "plain", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "STATE");
    BOOST_CHECK(!eng.Apply("VER-002", "RNG-s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "STATE");
    BOOST_CHECK(!eng.Apply("VER-002", "fp4", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.Apply("VER-002", "private-rng", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("VER-002"));

    BOOST_CHECK(!eng.Apply("VER-003", "sample", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "GAP");
    BOOST_CHECK(!eng.stored("VER-003"));

    BOOST_CHECK(!eng.Apply("VER-004", "self", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "SELF");
    BOOST_CHECK(!eng.stored("VER-004"));

    BOOST_CHECK(!eng.Apply("ADM-001", "one-root", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "QUORUM");
    BOOST_CHECK(!eng.stored("ADM-001"));

    BOOST_CHECK(!eng.Apply("ADM-002", "fp8", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.Apply("ADM-002", "not-a-path", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "PATH");
    BOOST_CHECK(!eng.stored("ADM-002"));
    BOOST_CHECK(!eng.broadcast());
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(views_paths_and_rng_store)
{
    for (const char* view : kViews) {
        modelnet::Acc03 eng;
        std::string err = "stale";
        BOOST_CHECK(eng.Apply("RES-003", view, static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK(eng.stored("RES-003"));
        BOOST_CHECK_EQUAL(eng.text("RES-003"), view);
        BOOST_CHECK(!eng.Apply("RES-003", view, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
        BOOST_CHECK_EQUAL(err, "REPLAY");
        BOOST_CHECK_EQUAL(eng.text("RES-003"), view);
    }

    for (const char* path : kPaths) {
        modelnet::Acc03 eng;
        std::string err = "stale";
        BOOST_CHECK(eng.Apply("ADM-002", path, static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK(eng.stored("ADM-002"));
        BOOST_CHECK_EQUAL(eng.text("ADM-002"), path);
        BOOST_CHECK(!eng.Apply("ADM-002", path, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
        BOOST_CHECK_EQUAL(eng.text("ADM-002"), path);
        BOOST_CHECK(!eng.Apply("ADM-002", "not-a-path", static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, "PATH");
        BOOST_CHECK_EQUAL(eng.text("ADM-002"), path);
    }

    modelnet::Acc03 eng;
    std::string err = "stale";
    BOOST_CHECK(eng.Apply("VER-002", "rng-s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.text("VER-002"), "rng-s1");
    BOOST_CHECK(!eng.Apply("VER-002", "rng-s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
    BOOST_CHECK(!eng.Apply("VER-002", "has-rng-inside", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "UNCHANGED");
    BOOST_CHECK(!eng.Apply("VER-002", "plain", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "STATE");
    BOOST_CHECK_EQUAL(eng.text("VER-002"), "rng-s1");
    BOOST_CHECK(!eng.gpu_passed());
    BOOST_CHECK(!eng.windows_passed());
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(every_known_id_release_stays_unqualified)
{
    modelnet::Acc03 eng;
    std::string err = "stale";

    for (const KnownRow& row : kKnown) {
        if (row.blocked != nullptr) {
            BOOST_CHECK(!eng.Apply(row.id, row.blocked, static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, row.blocked_err);
            BOOST_CHECK(!eng.stored(row.id));
        }
        BOOST_CHECK(eng.Apply(row.id, row.text, static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK(eng.stored(row.id));
        BOOST_CHECK_EQUAL(eng.text(row.id), row.text);
        BOOST_CHECK(!eng.Apply(row.id, row.text, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
        BOOST_CHECK_EQUAL(err, "REPLAY");
        BOOST_CHECK_EQUAL(eng.text(row.id), row.text);
        BOOST_CHECK(!eng.Apply(row.id, row.again, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, "UNCHANGED");
        BOOST_CHECK_EQUAL(eng.text(row.id), row.text);
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
    BOOST_CHECK_EQUAL(eng.text("VER-002"), "rng-s1");
    BOOST_CHECK_EQUAL(eng.text("RES-003"), "LEADING");
    BOOST_CHECK_EQUAL(eng.text("ADM-002"), "hostmodel");
    BOOST_CHECK(eng.stored("SCM-004"));
    BOOST_CHECK(!eng.stored("SCM-001"));
    BOOST_CHECK_EQUAL(eng.text("SCM-003"), "bounded-fetch");
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(hosts_stay_one)
{
    modelnet::Acc03 eng;
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
