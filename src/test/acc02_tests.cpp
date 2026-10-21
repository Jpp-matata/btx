// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/acc02/acc02.h>
#include <modelnet/nifdt_contract/contract.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>

namespace {

void Idle(modelnet::Acc02& eng)
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
};

const KnownRow kKnown[] = {
    {"WIRE-004", "s1"},
    {"ID-001", "s1"},
    {"ID-002", "s1"},
    {"ID-003", "s1"},
    {"ID-004", "s1"},
    {"NET-001", "s1"},
    {"NET-002", "s1"},
    {"NET-003", "s1"},
    {"NET-004", "s1"},
    {"GIT-001", "s1"},
    {"GIT-002", "s1"},
    {"GIT-003", "s1"},
    {"GIT-004", "s1"},
    {"GIT-005", "sha256-s1"},
    {"SCM-001", "s1"},
    {"SCM-002", "s1"},
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(acc02_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(id_text_aux_and_flag)
{
    modelnet::Acc02 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.Apply("", "", static_cast<int64_t>(0), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored(""));

    BOOST_CHECK(!eng.Apply("NO-SUCH", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("NO-SUCH"));
    BOOST_CHECK(eng.text("NO-SUCH").empty());

    BOOST_CHECK(!eng.Apply("GIT-006", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "ID");
    BOOST_CHECK(!eng.stored("GIT-006"));

    BOOST_CHECK(!eng.Apply("NET-004", "", static_cast<int64_t>(-1), false, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK(!eng.stored("NET-004"));

    BOOST_CHECK(!eng.Apply("NET-004", "s1", static_cast<int64_t>(-1), true, err));
    BOOST_CHECK_EQUAL(err, "BOUND");
    BOOST_CHECK(!eng.stored("NET-004"));

    BOOST_CHECK(!eng.Apply("NET-004", "s1", static_cast<int64_t>(101), false, err));
    BOOST_CHECK_EQUAL(err, "CAP");
    BOOST_CHECK(!eng.stored("NET-004"));

    BOOST_CHECK(!eng.Apply("NET-004", "s1", static_cast<int64_t>(0), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("NET-004"));

    BOOST_CHECK(eng.Apply("NET-004", "s1", static_cast<int64_t>(0), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK(eng.stored("NET-004"));
    BOOST_CHECK_EQUAL(eng.text("NET-004"), "s1");

    BOOST_CHECK(eng.Apply("SCM-001", "s1", static_cast<int64_t>(100), true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK(eng.stored("SCM-001"));
    BOOST_CHECK_EQUAL(eng.text("SCM-001"), "s1");
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(engine_private_and_order)
{
    modelnet::Acc02 eng;
    std::string err = "stale";
    const char* const names[] = {
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
    BOOST_CHECK(!modelnet::NifDtEngineLinked("dualpipe"));
    BOOST_CHECK(!modelnet::NifDtEngineLinked("vllm"));

    for (const char* name : names) {
        BOOST_CHECK(!eng.Apply("GIT-001", name, static_cast<int64_t>(1), true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
        BOOST_CHECK_EQUAL(err, "ENGINE_ABSENT");
        BOOST_CHECK(!eng.stored("GIT-001"));
        BOOST_CHECK(!eng.gpu());
        BOOST_CHECK(!eng.gpu_passed());
        BOOST_CHECK(!eng.windows_passed());
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
    }

    BOOST_CHECK(!eng.Apply("GIT-004", "private note", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK_EQUAL(err, "PRIVATE_DATA");
    BOOST_CHECK(!eng.stored("GIT-004"));

    BOOST_CHECK(!eng.Apply("WIRE-004", "private?", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("WIRE-004"));

    BOOST_CHECK(!eng.Apply("ID-001", "private-note", static_cast<int64_t>(0), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_PRIVATE_DATA);
    BOOST_CHECK(!eng.stored("ID-001"));

    BOOST_CHECK(!eng.Apply("NET-003", "s1", static_cast<int64_t>(101), true, err));
    BOOST_CHECK_EQUAL(err, "CAP");
    BOOST_CHECK(!eng.stored("NET-003"));

    BOOST_CHECK(!eng.Apply("NET-003", "s1", static_cast<int64_t>(9), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("NET-003"));

    BOOST_CHECK(!eng.Apply("NET-003", "vllm", static_cast<int64_t>(9), true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_ENGINE_ABSENT);
    BOOST_CHECK(!eng.stored("NET-003"));

    BOOST_CHECK(!eng.Apply("ID-001", "s1", static_cast<int64_t>(0), false, err));
    BOOST_CHECK_EQUAL(err, "ABSENT");
    BOOST_CHECK(!eng.stored("ID-001"));

    BOOST_CHECK(!eng.Apply("ID-002", "spend", static_cast<int64_t>(0), true, err));
    BOOST_CHECK_EQUAL(err, "ROLE");
    BOOST_CHECK(!eng.stored("ID-002"));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(every_known_id_release_stays_unqualified)
{
    modelnet::Acc02 eng;
    std::string err = "stale";

    for (const KnownRow& row : kKnown) {
        const std::string id(row.id);
        int64_t aux = static_cast<int64_t>(1);
        const char* other = "s2";
        if (id == "WIRE-004") {
            BOOST_CHECK(!eng.Apply(row.id, "a?b", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "URI");
            BOOST_CHECK(!eng.stored(row.id));
            BOOST_CHECK(!eng.Apply(row.id, "a#b", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "URI");
            BOOST_CHECK(!eng.stored(row.id));
        } else if (id == "ID-001") {
            BOOST_CHECK(!eng.Apply(row.id, "s1", static_cast<int64_t>(0), true, err));
            BOOST_CHECK_EQUAL(err, "EXPIRED");
            BOOST_CHECK(!eng.stored(row.id));
        } else if (id == "ID-002") {
            BOOST_CHECK(!eng.Apply(row.id, "spend", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "ROLE");
            BOOST_CHECK(!eng.stored(row.id));
        } else if (id == "ID-003") {
            BOOST_CHECK(!eng.Apply(row.id, "widen", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "NARROW");
            BOOST_CHECK(!eng.stored(row.id));
        } else if (id == "ID-004") {
            BOOST_CHECK(!eng.Apply(row.id, "shrink", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "THRESHOLD");
            BOOST_CHECK(!eng.stored(row.id));
        } else if (id == "NET-001") {
            BOOST_CHECK(!eng.Apply(row.id, "tofu", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "TOFU");
            BOOST_CHECK(!eng.stored(row.id));
        } else if (id == "NET-002") {
            BOOST_CHECK(!eng.Apply(row.id, "downgrade", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "DOWNGRADE");
            BOOST_CHECK(!eng.stored(row.id));
        } else if (id == "NET-003") {
            BOOST_CHECK(!eng.Apply(row.id, "s1", static_cast<int64_t>(9), true, err));
            BOOST_CHECK_EQUAL(err, "FANOUT");
            BOOST_CHECK(!eng.stored(row.id));
            BOOST_CHECK(!eng.Apply(row.id, "s1", static_cast<int64_t>(100), true, err));
            BOOST_CHECK_EQUAL(err, "FANOUT");
            BOOST_CHECK(!eng.stored(row.id));
            aux = static_cast<int64_t>(8);
        } else if (id == "GIT-002") {
            BOOST_CHECK(!eng.Apply(row.id, "sha1", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "DIGEST");
            BOOST_CHECK(!eng.stored(row.id));
        } else if (id == "GIT-003") {
            BOOST_CHECK(!eng.Apply(row.id, "hook", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "HOOK");
            BOOST_CHECK(!eng.stored(row.id));
        } else if (id == "GIT-005") {
            BOOST_CHECK(!eng.Apply(row.id, "s1", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "OID");
            BOOST_CHECK(!eng.stored(row.id));
            BOOST_CHECK(!eng.Apply(row.id, "sha1", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "OID");
            BOOST_CHECK(!eng.stored(row.id));
            other = "sha256-s2";
        } else if (id == "SCM-002") {
            BOOST_CHECK(!eng.Apply(row.id, "conflict", static_cast<int64_t>(1), true, err));
            BOOST_CHECK_EQUAL(err, "CONFLICT");
            BOOST_CHECK(!eng.stored(row.id));
        }

        BOOST_CHECK(eng.Apply(row.id, row.text, aux, true, err));
        BOOST_CHECK(err.empty());
        BOOST_CHECK(eng.stored(row.id));
        BOOST_CHECK_EQUAL(eng.text(row.id), row.text);
        BOOST_CHECK_EQUAL(eng.classification(), "SIMULATION_NOT_HARDWARE_MEASUREMENT");
        BOOST_CHECK(!eng.Apply(row.id, row.text, aux, true, err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_REPLAY);
        BOOST_CHECK_EQUAL(err, "REPLAY");
        BOOST_CHECK_EQUAL(eng.text(row.id), row.text);
        BOOST_CHECK(!eng.Apply(row.id, other, aux, true, err));
        BOOST_CHECK_EQUAL(err, "UNCHANGED");
        BOOST_CHECK_EQUAL(eng.text(row.id), row.text);
        BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
        BOOST_CHECK_EQUAL(eng.physical_hosts(), static_cast<int64_t>(1));
        BOOST_CHECK_EQUAL(eng.bytes_moved(), static_cast<int64_t>(0));
        BOOST_CHECK(!eng.broadcast());
        BOOST_CHECK(!eng.gpu_passed());
        BOOST_CHECK(!eng.windows_passed());
    }

    BOOST_CHECK(!eng.Apply("WIRE-004", "s1?", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "URI");
    BOOST_CHECK_EQUAL(eng.text("WIRE-004"), "s1");
    BOOST_CHECK(!eng.Apply("ID-001", "s1", static_cast<int64_t>(0), true, err));
    BOOST_CHECK_EQUAL(err, "EXPIRED");
    BOOST_CHECK_EQUAL(eng.text("ID-001"), "s1");
    BOOST_CHECK(!eng.Apply("GIT-005", "s1", static_cast<int64_t>(1), true, err));
    BOOST_CHECK_EQUAL(err, "OID");
    BOOST_CHECK_EQUAL(eng.text("GIT-005"), "sha256-s1");

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
    BOOST_CHECK(eng.stored("WIRE-004"));
    BOOST_CHECK_EQUAL(eng.text("WIRE-004"), "s1");
    BOOST_CHECK(eng.stored("ID-001"));
    BOOST_CHECK(eng.stored("ID-002"));
    BOOST_CHECK(eng.stored("ID-003"));
    BOOST_CHECK(eng.stored("ID-004"));
    BOOST_CHECK(eng.stored("NET-001"));
    BOOST_CHECK(eng.stored("NET-002"));
    BOOST_CHECK(eng.stored("NET-003"));
    BOOST_CHECK_EQUAL(eng.text("NET-003"), "s1");
    BOOST_CHECK(eng.stored("NET-004"));
    BOOST_CHECK(eng.stored("GIT-001"));
    BOOST_CHECK(eng.stored("GIT-002"));
    BOOST_CHECK(eng.stored("GIT-003"));
    BOOST_CHECK(eng.stored("GIT-004"));
    BOOST_CHECK(eng.stored("GIT-005"));
    BOOST_CHECK_EQUAL(eng.text("GIT-005"), "sha256-s1");
    BOOST_CHECK(eng.stored("SCM-001"));
    BOOST_CHECK(eng.stored("SCM-002"));
    BOOST_CHECK_EQUAL(eng.text("SCM-002"), "s1");
    BOOST_CHECK(!eng.stored("NO-SUCH"));
    BOOST_CHECK(!eng.stored("GIT-006"));
    Idle(eng);
}

BOOST_AUTO_TEST_CASE(hosts_stay_one)
{
    modelnet::Acc02 eng;
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
