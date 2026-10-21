// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc05/acc05.h>
#include <modelnet/nifdt_contract/contract.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>

namespace {

void Idle(modelnet::Acc05& eng)
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
    BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
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
    BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
    BOOST_CHECK_EQUAL(eng.classification(), "SIMULATION_NOT_HARDWARE_MEASUREMENT");
}

struct KnownRow {
    const char* id;
    const char* text;
    const char* again;
    const char* refuse;
    const char* refuse_err;
};

const KnownRow kKnown[] = {
    {"WAN-002", "staged", "active", "other-state", "STATE"},
    {"WAN-003", "single", "uniform", "mixed", "GENERATION"},
    {"WAN-004", "noted", "other-sentence", nullptr, nullptr},
    {"WAN-005", "archive", "packed", "symlink", "TAR"},
    {"ENT-001", "local", "member-path", "rpc", "ROUTE"},
    {"ENT-002", "member", "seat", nullptr, nullptr},
    {"ENT-003", "review-held", "noted", "endorse", "REVIEW"},
    {"ECON-001", "quote", "meter", "spend", "WALLET"},
    {"ECON-002", "meter", "quote", nullptr, nullptr},
    {"STORE-001", "object", "blob", nullptr, nullptr},
    {"STORE-002", "once", "later", "retry", "UNKNOWN"},
    {"STORE-003", "signed", "sealed", "skip-sig", "LIMIT"},
    {"API-001", "idempotency-s1", "idempotency-s2", "s1", "IDEMPOTENT"},
    {"API-002", "route-s1", "route-s2", nullptr, nullptr},
    {"API-003", "research", "model", "other", "CLI"},
    {"API-004", "shown", "plain", "trusted", "BADGE"},
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

const char* const kWan002[] = {
    "discovered",
    "fetching",
    "staged",
    "verified",
    "canary",
    "active",
    "paused",
    "failed",
    "rolled_back",
    "unknown",
};

const char* const kApi003[] = {
    "identity",
    "git",
    "workspace",
    "research",
    "evidence",
    "model",
    "agent",
    "realm",
    "promotion",
    "channel",
    "rollout",
    "bundle",
    "migration",
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(acc05_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(id_text_aux_and_flag)
{
    modelnet::Acc05 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.Apply("", "", static_cast<int64_t>(0), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored(""));

    BOOST_CHECK(!eng.Apply("NO-SUCH", "staged", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("NO-SUCH"));

    BOOST_CHECK(!eng.Apply("WAN-001", "staged", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("WAN-001"));

    BOOST_CHECK(!eng.Apply("WAN-002", "", static_cast<int64_t>(-1), false, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK(!eng.stored("WAN-002"));

    BOOST_CHECK(!eng.Apply("WAN-002", "staged", static_cast<int64_t>(-1), true, err));
    BOOST_CHECK_EQUAL(err, "BOUND");
    BOOST_CHECK(!eng.stored("WAN-002"));

    BOOST_CHECK(!eng.Apply("WAN-002", "staged", static_cast<int64_t>(101), false, err));
    BOOST_CHECK_EQUAL(err, "CAP");
    BOOST_CHECK(!eng.stored("WAN-002"));

    BOOST_CHECK(!eng.Apply("WAN-002", "staged", static_cast<int64_t>(0), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("WAN-002"));

    BOOST_CHECK(eng.Apply("WAN-002", "staged", static_cast<int64_t>(100), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK(eng.stored("WAN-002"));
    BOOST_CHECK_EQUAL(eng.text("WAN-002"), "staged");
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(engines_private_and_id_rules)
{
    modelnet::Acc05 eng;
    std::string err = "stale";

    BOOST_CHECK(modelnet::NifDtEngineLinked("fp8"));
    BOOST_CHECK(!eng.Apply("WAN-004", "fp8", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK_EQUAL(err, "ENGINE_ABSENT");
    BOOST_CHECK(!eng.stored("WAN-004"));
    BOOST_CHECK(!eng.gpu());
    BOOST_CHECK(!eng.gpu_passed());

    BOOST_CHECK(modelnet::NifDtEngineLinked("fp4"));
    BOOST_CHECK(!eng.Apply("WAN-004", "fp4", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.stored("WAN-004"));

    BOOST_CHECK(!modelnet::NifDtEngineLinked("dualpipe"));
    BOOST_CHECK(!eng.Apply("WAN-004", "dualpipe", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.stored("WAN-004"));

    for (const char* name : kEngines) {
        BOOST_CHECK(!eng.Apply("WAN-004", name, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
        BOOST_CHECK(!eng.stored("WAN-004"));
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
        BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
    }

    BOOST_CHECK(!eng.Apply("WAN-002", "vllm", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.stored("WAN-002"));

    BOOST_CHECK(!eng.Apply("WAN-004", "private trajectory", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK_EQUAL(err, "PRIVATE_DATA");
    BOOST_CHECK(!eng.stored("WAN-004"));

    BOOST_CHECK(!eng.Apply("API-001", "private-idempotency", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("API-001"));

    BOOST_CHECK(!eng.Apply("WAN-002", "other-state", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "STATE");
    BOOST_CHECK(!eng.stored("WAN-002"));

    BOOST_CHECK(!eng.Apply("WAN-003", "mixed", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "GENERATION");
    BOOST_CHECK(!eng.stored("WAN-003"));
    BOOST_CHECK(!eng.Apply("WAN-003", "mixed", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "GENERATION");
    BOOST_CHECK(!eng.stored("WAN-003"));

    BOOST_CHECK(!eng.Apply("WAN-005", "symlink", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "TAR");
    BOOST_CHECK(!eng.stored("WAN-005"));

    BOOST_CHECK(!eng.Apply("ENT-001", "rpc", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ROUTE");
    BOOST_CHECK(!eng.stored("ENT-001"));

    BOOST_CHECK(!eng.Apply("ENT-003", "endorse", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "REVIEW");
    BOOST_CHECK(!eng.stored("ENT-003"));

    BOOST_CHECK(!eng.Apply("ECON-001", "spend", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "WALLET");
    BOOST_CHECK(!eng.stored("ECON-001"));

    BOOST_CHECK(!eng.Apply("STORE-002", "retry", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "UNKNOWN");
    BOOST_CHECK(!eng.stored("STORE-002"));

    BOOST_CHECK(!eng.Apply("STORE-003", "skip-sig", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "LIMIT");
    BOOST_CHECK(!eng.stored("STORE-003"));

    BOOST_CHECK(!eng.Apply("API-001", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "IDEMPOTENT");
    BOOST_CHECK(!eng.stored("API-001"));

    BOOST_CHECK(!eng.Apply("API-003", "other", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "CLI");
    BOOST_CHECK(!eng.stored("API-003"));

    BOOST_CHECK(!eng.Apply("API-003", "fp8", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.stored("API-003"));

    BOOST_CHECK(!eng.Apply("API-004", "trusted", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "BADGE");
    BOOST_CHECK(!eng.stored("API-004"));
    BOOST_CHECK(!eng.broadcast());
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(every_known_id_release_stays_unqualified)
{
    modelnet::Acc05 eng;
    std::string err = "stale";

    for (const char* state : kWan002) {
        modelnet::Acc05 one;
        BOOST_CHECK(one.Apply("WAN-002", state, static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK_EQUAL(one.text("WAN-002"), state);
        BOOST_CHECK_EQUAL(one.atoms_moved(), static_cast<int64_t>(0));
    }

    for (const char* name : kApi003) {
        modelnet::Acc05 one;
        BOOST_CHECK(one.Apply("API-003", name, static_cast<int64_t>(1), true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK_EQUAL(one.text("API-003"), name);
    }

    for (const KnownRow& row : kKnown) {
        if (row.refuse != nullptr) {
            BOOST_CHECK(!eng.Apply(row.id, row.refuse, static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, row.refuse_err);
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
        if (row.refuse != nullptr) {
            BOOST_CHECK(!eng.Apply(row.id, row.refuse, static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, row.refuse_err);
            BOOST_CHECK_EQUAL(eng.text(row.id), row.text);
        }
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
        BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
    }

    BOOST_CHECK(eng.stored("WAN-002"));
    BOOST_CHECK_EQUAL(eng.text("WAN-002"), "staged");
    BOOST_CHECK(eng.stored("API-001"));
    BOOST_CHECK_EQUAL(eng.text("API-001"), "idempotency-s1");
    BOOST_CHECK(eng.stored("API-003"));
    BOOST_CHECK_EQUAL(eng.text("API-003"), "research");

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
    BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
    BOOST_CHECK(!eng.Spend(err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_AUTOMATIC_SPEND);
    BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(hosts_stay_one)
{
    modelnet::Acc05 eng;
    std::string err = "stale";
    BOOST_CHECK(!eng.Hosts(static_cast<int64_t>(2), err));
    BOOST_CHECK_EQUAL(err, "HOSTS");
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.physical_hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
    BOOST_CHECK(!eng.Hosts(static_cast<int64_t>(0), err));
    BOOST_CHECK_EQUAL(err, "HOSTS");
    BOOST_CHECK(eng.Hosts(static_cast<int64_t>(1), err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
    BOOST_CHECK(!eng.broadcast());
    Idle(eng);
}

BOOST_AUTO_TEST_SUITE_END()
