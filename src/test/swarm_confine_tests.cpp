// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_confine.h>
#include <test/util/setup_common.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <sys/stat.h>
#include <sys/wait.h>

#include <fstream>
#include <string>

namespace {

modelnet::SwarmConfineRequest BaseRequest(const fs::path& helper, const fs::path& root)
{
    modelnet::SwarmConfineRequest request;
    request.helper = helper;
    request.root = root;
    request.max_address_bytes = 8LL * 1024 * 1024 * 1024 * 1024;
    request.max_cpu_seconds = 30;
    request.max_nofile = 64;
    request.network_denied = true;
    return request;
}

void ExpectRefusal(const modelnet::SwarmConfineRequest& request, const char* code)
{
    std::string err = "stale";
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    BOOST_CHECK(!result.launched);
    BOOST_CHECK(!result.limits_applied);
    BOOST_CHECK_EQUAL(result.child_status, -1);
    BOOST_CHECK_EQUAL(result.code, code);
    BOOST_CHECK_EQUAL(err, code);
}

fs::path WriteRegularFile(const fs::path& path)
{
    fs::create_directories(path.parent_path());
    std::ofstream out{path};
    BOOST_REQUIRE(out);
    out << "probe\n";
    BOOST_REQUIRE(out);
    return path;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(swarm_confine_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(empty_helper_or_root)
{
    ExpectRefusal(BaseRequest(fs::path{}, fs::temp_directory_path()), "CONFINEMENT");
    const fs::path helper = WriteRegularFile(fs::temp_directory_path() / "btx-swarm-confine-empty-helper");
    ExpectRefusal(BaseRequest(helper, fs::path{}), "CONFINEMENT");
    fs::remove(helper);
}

BOOST_AUTO_TEST_CASE(helper_not_regular_file)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-confine-notfile";
    const fs::path directory = root / "directory";
    fs::create_directories(directory);
    ExpectRefusal(BaseRequest(directory, root), "SANDBOX_ABSENT");
    ExpectRefusal(BaseRequest(root / "missing", root), "SANDBOX_ABSENT");
    fs::remove_all(root);
}

BOOST_AUTO_TEST_CASE(network_not_denied)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-confine-network";
    const fs::path helper = WriteRegularFile(root / "helper.sh");
    modelnet::SwarmConfineRequest request = BaseRequest(helper, root);
    request.network_denied = false;
    ExpectRefusal(request, "NETWORK");
    fs::remove_all(root);
}

BOOST_AUTO_TEST_CASE(non_positive_limits)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-confine-limit";
    const fs::path helper = WriteRegularFile(root / "helper.sh");
    modelnet::SwarmConfineRequest address = BaseRequest(helper, root);
    address.max_address_bytes = 0;
    ExpectRefusal(address, "LIMIT");
    modelnet::SwarmConfineRequest cpu = BaseRequest(helper, root);
    cpu.max_cpu_seconds = 0;
    ExpectRefusal(cpu, "LIMIT");
    modelnet::SwarmConfineRequest nofile = BaseRequest(helper, root);
    nofile.max_nofile = -1;
    ExpectRefusal(nofile, "LIMIT");
    fs::remove_all(root);
}

BOOST_AUTO_TEST_CASE(path_outside_or_dotdot)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-confine-path";
    const fs::path helper = WriteRegularFile(root / "helper.sh");
    const fs::path outside_root = fs::temp_directory_path() / "btx-swarm-confine-outside";
    const fs::path outside = WriteRegularFile(outside_root / "helper.sh");
    ExpectRefusal(BaseRequest(outside, root), "PATH");

    const fs::path dotted = root / ".." / root.filename() / "helper.sh";
    BOOST_REQUIRE(fs::exists(dotted));
    BOOST_REQUIRE(fs::is_regular_file(dotted));
    ExpectRefusal(BaseRequest(dotted, root), "PATH");

    fs::remove_all(root);
    fs::remove_all(outside_root);
}

BOOST_AUTO_TEST_CASE(confined_helper_or_namespace_absent)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-confine-run";
    const fs::path helper = root / "helper.sh";
    fs::create_directories(root);
    {
        std::ofstream out{helper};
        BOOST_REQUIRE(out);
        out << "#!/bin/sh\n";
        out << "echo ok\n";
        BOOST_REQUIRE(out);
    }
    BOOST_REQUIRE(chmod(helper.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0);

    std::string err = "stale";
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(BaseRequest(helper, root), err);
    fs::remove_all(root);

    const bool confined = result.code == "CONFINED" && result.launched && result.limits_applied && err.empty();
    const bool absent = result.code == "CONFINEMENT_ABSENT" && !result.launched && !result.limits_applied &&
                        err == "CONFINEMENT_ABSENT";
    BOOST_CHECK(confined || absent);
    BOOST_CHECK(!(result.launched && result.code == "CONFINEMENT_ABSENT"));
    if (confined) {
        BOOST_CHECK(WIFEXITED(result.child_status));
        BOOST_CHECK_EQUAL(WEXITSTATUS(result.child_status), 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
