// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_confine.h>
#include <test/util/setup_common.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <sys/stat.h>
#include <sys/wait.h>

#include <csignal>

#include <fstream>
#include <iterator>
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

#if defined(__APPLE__) && defined(BTX_SWARM_JAIL_PROBE)
namespace {

fs::path CopyProbe(const fs::path& root, const char* marker)
{
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path helper = root / "probe";
    fs::copy_file(fs::path{BTX_SWARM_JAIL_PROBE}, helper, fs::copy_options::overwrite_existing);
    BOOST_REQUIRE(chmod(helper.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0);
    if (marker != nullptr) {
        WriteRegularFile(root / marker);
    }
    return helper;
}

std::string ProbeOutput(const fs::path& root)
{
    std::ifstream in{root / "probe.out"};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace

// macOS: kernel sandbox instead of a network namespace. The helper runs,
// cannot bind a loopback socket, and sees the CPU and descriptor limits.
BOOST_AUTO_TEST_CASE(macos_helper_runs_confined)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-confine-macos";
    const fs::path helper = CopyProbe(root, nullptr);
    modelnet::SwarmConfineRequest request = BaseRequest(helper, root);
    request.max_address_bytes = 1LL << 30;
    std::string err = "stale";
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    const std::string out = ProbeOutput(root);
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINED");
    BOOST_CHECK(result.launched && result.limits_applied && err.empty());
    BOOST_CHECK(WIFEXITED(result.child_status));
    BOOST_CHECK_EQUAL(WEXITSTATUS(result.child_status), 0);
    BOOST_CHECK(out.find("loopback_bind=0\n") != std::string::npos);
    BOOST_CHECK(out.find("nofile=64\n") != std::string::npos);
    BOOST_CHECK(out.find("cpu=30\n") != std::string::npos);
}

// macOS ignores RLIMIT_AS; the parent kills a child whose footprint passes
// max_address_bytes.
BOOST_AUTO_TEST_CASE(macos_address_limit_enforced)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-confine-macos-alloc";
    const fs::path helper = CopyProbe(root, "alloc.mode");
    modelnet::SwarmConfineRequest request = BaseRequest(helper, root);
    request.max_address_bytes = 256LL << 20;
    std::string err;
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINED");
    BOOST_CHECK(WIFSIGNALED(result.child_status));
    BOOST_CHECK_EQUAL(WTERMSIG(result.child_status), SIGKILL);
}

// RLIMIT_CPU is enforced by the kernel: SIGXCPU at the limit (SIGKILL if
// the hard limit is passed first).
BOOST_AUTO_TEST_CASE(macos_cpu_limit_enforced)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-confine-macos-spin";
    const fs::path helper = CopyProbe(root, "spin.mode");
    modelnet::SwarmConfineRequest request = BaseRequest(helper, root);
    request.max_cpu_seconds = 1;
    std::string err;
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINED");
    BOOST_CHECK(WIFSIGNALED(result.child_status));
    BOOST_CHECK(WTERMSIG(result.child_status) == SIGXCPU || WTERMSIG(result.child_status) == SIGKILL);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
