// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/nifdt_contract/contract.h>
#include <modelnet/pay02/pay02.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <string>

BOOST_FIXTURE_TEST_SUITE(pay02_tests, BasicTestingSetup)

static void ExpectClosed(const modelnet::Pay02& eng)
{
    BOOST_CHECK(!eng.settled());
    BOOST_CHECK(!eng.passed());
    BOOST_CHECK(!eng.release_qualified());
    BOOST_CHECK(!eng.gpu_passed());
    BOOST_CHECK(!eng.windows_passed());
    BOOST_CHECK(!eng.hardware_qualified());
    BOOST_CHECK_EQUAL(eng.hosts(), static_cast<int64_t>(1));
    BOOST_CHECK_EQUAL(eng.atoms_moved(), static_cast<int64_t>(0));
    BOOST_CHECK_EQUAL(eng.automatic_spend(), modelnet::NIFDT_AUTOMATIC_SPEND);
    BOOST_CHECK_EQUAL(eng.automatic_spend(), "0");
}

BOOST_AUTO_TEST_CASE(record_funding_stores_paths_without_settlement)
{
    modelnet::Pay02 eng;
    std::string err = "stale";

    BOOST_CHECK(eng.objective().empty());
    BOOST_CHECK(!eng.opened());
    BOOST_CHECK(!eng.claim_kept("case-1"));
    ExpectClosed(eng);

    BOOST_CHECK(!eng.RecordFunding("", true, true, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK(eng.objective().empty());
    BOOST_CHECK(!eng.opened());
    BOOST_CHECK(!eng.claim_kept(""));

    err = "stale";
    BOOST_CHECK(!eng.RecordFunding("", false, false, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK(eng.objective().empty());

    err = "stale";
    BOOST_CHECK(!eng.RecordFunding("case-1", false, true, err));
    BOOST_CHECK_EQUAL(err, "PATH");
    BOOST_CHECK(eng.objective().empty());
    BOOST_CHECK(!eng.opened());
    BOOST_CHECK(!eng.claim_kept("case-1"));

    err = "stale";
    BOOST_CHECK(!eng.RecordFunding("case-1", true, false, err));
    BOOST_CHECK_EQUAL(err, "PATH");
    BOOST_CHECK(eng.objective().empty());
    BOOST_CHECK(!eng.claim_kept("case-1"));

    err = "stale";
    BOOST_CHECK(!eng.RecordFunding("case-1", false, false, err));
    BOOST_CHECK_EQUAL(err, "PATH");
    BOOST_CHECK(eng.objective().empty());
    BOOST_CHECK(!eng.opened());

    err = "stale";
    BOOST_CHECK(eng.RecordFunding("case-1", true, true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.objective(), "case-1");
    BOOST_CHECK(eng.opened());
    BOOST_CHECK(eng.claim_kept("case-1"));
    BOOST_CHECK(!eng.claim_kept("case-2"));
    ExpectClosed(eng);

    err = "stale";
    BOOST_CHECK(!eng.RecordFunding("case-2", false, true, err));
    BOOST_CHECK_EQUAL(err, "PATH");
    BOOST_CHECK_EQUAL(eng.objective(), "case-1");
    BOOST_CHECK(eng.claim_kept("case-1"));
    BOOST_CHECK(!eng.claim_kept("case-2"));

    err = "stale";
    BOOST_CHECK(!eng.RecordFunding("", true, true, err));
    BOOST_CHECK_EQUAL(err, "FIELD");
    BOOST_CHECK_EQUAL(eng.objective(), "case-1");

    err = "stale";
    BOOST_CHECK(eng.RecordFunding("case-2", true, true, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(eng.objective(), "case-2");
    BOOST_CHECK(eng.opened());
    BOOST_CHECK(eng.claim_kept("case-1"));
    BOOST_CHECK(eng.claim_kept("case-2"));
    ExpectClosed(eng);
}

BOOST_AUTO_TEST_CASE(prove_exit_stays_unqualified)
{
    modelnet::Pay02 eng;
    std::string err = "stale";

    BOOST_CHECK(eng.RecordFunding("case-1", true, true, err));
    BOOST_CHECK_EQUAL(eng.objective(), "case-1");

    const bool flags[][5] = {
        {false, false, false, false, false},
        {false, true, true, true, true},
        {true, false, true, true, true},
        {true, true, false, true, true},
        {true, true, true, false, true},
        {true, true, true, true, false},
        {true, true, true, true, true},
    };
    for (const auto& row : flags) {
        err = "stale";
        BOOST_CHECK(!eng.ProveExit(row[0], row[1], row[2], row[3], row[4], err));
        BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    }
    BOOST_CHECK_EQUAL(eng.objective(), "case-1");
    BOOST_CHECK(eng.claim_kept("case-1"));
    ExpectClosed(eng);
}

BOOST_AUTO_TEST_CASE(federate_refuses_a_second_organization)
{
    modelnet::Pay02 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.Federate(0, true, err));
    BOOST_CHECK_EQUAL(err, "ORG");
    BOOST_CHECK(!eng.Federate(1, true, err));
    BOOST_CHECK_EQUAL(err, "ORG");
    BOOST_CHECK(!eng.Federate(-1, true, err));
    BOOST_CHECK_EQUAL(err, "ORG");
    BOOST_CHECK(!eng.Federate(2, false, err));
    BOOST_CHECK_EQUAL(err, "ORG");
    BOOST_CHECK(!eng.Federate(4, false, err));
    BOOST_CHECK_EQUAL(err, "ORG");
    BOOST_CHECK(!eng.Federate(1, false, err));
    BOOST_CHECK_EQUAL(err, "ORG");

    err = "stale";
    BOOST_CHECK(!eng.Federate(2, true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK(!eng.Federate(3, true, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK(eng.objective().empty());
    BOOST_CHECK(!eng.opened());
    ExpectClosed(eng);
}

BOOST_AUTO_TEST_CASE(cross_host_faults_stay_unqualified)
{
    modelnet::Pay02 eng;
    std::string err = "stale";

    BOOST_CHECK(!eng.ExerciseFaults(true, true, true, true, 0, err));
    BOOST_CHECK_EQUAL(err, "HOSTS");
    BOOST_CHECK(!eng.ExerciseFaults(true, true, true, true, 1, err));
    BOOST_CHECK_EQUAL(err, "HOSTS");
    BOOST_CHECK(!eng.ExerciseFaults(false, true, true, true, 1, err));
    BOOST_CHECK_EQUAL(err, "HOSTS");

    err = "stale";
    BOOST_CHECK(!eng.ExerciseFaults(false, true, true, true, 2, err));
    BOOST_CHECK_EQUAL(err, "FAULT");
    BOOST_CHECK(!eng.ExerciseFaults(true, false, true, true, 2, err));
    BOOST_CHECK_EQUAL(err, "FAULT");
    BOOST_CHECK(!eng.ExerciseFaults(true, true, false, true, 2, err));
    BOOST_CHECK_EQUAL(err, "FAULT");
    BOOST_CHECK(!eng.ExerciseFaults(true, true, true, false, 3, err));
    BOOST_CHECK_EQUAL(err, "FAULT");
    BOOST_CHECK(!eng.ExerciseFaults(false, false, false, false, 2, err));
    BOOST_CHECK_EQUAL(err, "FAULT");

    err = "stale";
    BOOST_CHECK(!eng.ExerciseFaults(true, true, true, true, 2, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK(!eng.ExerciseFaults(true, true, true, true, 4, err));
    BOOST_CHECK_EQUAL(err, modelnet::NIFDT_UNQUALIFIED);
    BOOST_CHECK(eng.objective().empty());
    BOOST_CHECK(!eng.opened());
    ExpectClosed(eng);
}

BOOST_AUTO_TEST_SUITE_END()
