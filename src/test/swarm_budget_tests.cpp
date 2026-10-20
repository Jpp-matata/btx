// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_budget.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(swarm_budget_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(open_spend_exhaust_leaves_remainder)
{
    modelnet::SwarmBudget budget;
    std::string err = "stale";

    BOOST_CHECK(!budget.Open("", 4, err));
    BOOST_CHECK_EQUAL(err, "EXPERIMENT");
    BOOST_CHECK_EQUAL(budget.Remaining(""), -1);

    BOOST_CHECK(!budget.Open("alpha", 0, err));
    BOOST_CHECK_EQUAL(err, "BUDGET");
    BOOST_CHECK_EQUAL(budget.Remaining("alpha"), -1);

    BOOST_CHECK(!budget.Open("alpha", -3, err));
    BOOST_CHECK_EQUAL(err, "BUDGET");
    BOOST_CHECK_EQUAL(budget.Remaining("alpha"), -1);

    BOOST_REQUIRE(budget.Open("alpha", 5, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(budget.Remaining("alpha"), 5);

    BOOST_CHECK(!budget.Open("alpha", 9, err));
    BOOST_CHECK_EQUAL(err, "REPLAY");
    BOOST_CHECK_EQUAL(budget.Remaining("alpha"), 5);

    BOOST_CHECK(!budget.Spend("missing", 1, err));
    BOOST_CHECK_EQUAL(err, "EXPERIMENT");
    BOOST_CHECK_EQUAL(budget.Remaining("missing"), -1);
    BOOST_CHECK_EQUAL(budget.Remaining("alpha"), 5);

    BOOST_CHECK(!budget.Spend("alpha", 0, err));
    BOOST_CHECK_EQUAL(err, "BUDGET");
    BOOST_CHECK_EQUAL(budget.Remaining("alpha"), 5);

    BOOST_REQUIRE(budget.Spend("alpha", 2, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(budget.Remaining("alpha"), 3);

    BOOST_CHECK(!budget.Spend("alpha", 4, err));
    BOOST_CHECK_EQUAL(err, "EXHAUSTED");
    BOOST_CHECK_EQUAL(budget.Remaining("alpha"), 3);

    BOOST_REQUIRE(budget.Spend("alpha", 3, err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(budget.Remaining("alpha"), 0);

    BOOST_CHECK(!budget.Spend("alpha", 1, err));
    BOOST_CHECK_EQUAL(err, "EXHAUSTED");
    BOOST_CHECK_EQUAL(budget.Remaining("alpha"), 0);
}

BOOST_AUTO_TEST_SUITE_END()
