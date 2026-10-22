// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_confine.h>
#include <test/util/setup_common.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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
#include <fcntl.h>
#include <stdlib.h>

namespace {

// Runs `run` with stdin replaced by a new pty that is not this process's
// controlling terminal (O_NOCTTY), then restores stdin.
template <typename Run>
void WithPtyStdin(Run run)
{
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    BOOST_REQUIRE(master >= 0);
    BOOST_REQUIRE(grantpt(master) == 0 && unlockpt(master) == 0);
    const int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    BOOST_REQUIRE(slave >= 0);
    const int saved = dup(STDIN_FILENO);
    BOOST_REQUIRE(dup2(slave, STDIN_FILENO) == STDIN_FILENO);
    run();
    if (saved >= 0) {
        dup2(saved, STDIN_FILENO);
        close(saved);
    } else {
        close(STDIN_FILENO);
    }
    close(slave);
    close(master);
}

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
    // A connected socket that is not close-on-exec, like btxd's peers.
    int pair[2];
    BOOST_REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    std::string err = "stale";
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    close(pair[0]);
    close(pair[1]);
    const std::string out = ProbeOutput(root);
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINED");
    BOOST_CHECK(result.launched && result.limits_applied && err.empty());
    BOOST_CHECK(WIFEXITED(result.child_status));
    BOOST_CHECK_EQUAL(WEXITSTATUS(result.child_status), 0);
    BOOST_CHECK(out.find("loopback_bind=0\n") != std::string::npos);
    BOOST_CHECK(out.find("nofile=64\n") != std::string::npos);
    BOOST_CHECK(out.find("cpu=30\n") != std::string::npos);
    // No fork: the parent enforces the limits on the one process it watches.
    BOOST_CHECK(out.find("fork=0\n") != std::string::npos);
    BOOST_CHECK(out.find("inherited_fds=0\n") != std::string::npos);
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

// The kernel only sends SIGXCPU at RLIMIT_CPU. A helper that ignores it is
// killed by the parent shortly after the limit, not after its own 20 s.
BOOST_AUTO_TEST_CASE(macos_cpu_limit_enforced_when_sigxcpu_ignored)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-confine-macos-spinignore";
    const fs::path helper = CopyProbe(root, "spinignore.mode");
    modelnet::SwarmConfineRequest request = BaseRequest(helper, root);
    request.max_cpu_seconds = 1;
    std::string err;
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINED");
    BOOST_CHECK(WIFSIGNALED(result.child_status));
    BOOST_CHECK_EQUAL(WTERMSIG(result.child_status), SIGKILL);
}

// The helper may not drive a terminal it inherits: TIOCSTI on a pty stdin
// is refused by the sandbox (EPERM), not only by the tty driver (EACCES,
// since the pty is not the helper's controlling terminal).
BOOST_AUTO_TEST_CASE(macos_terminal_ioctl_denied)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-confine-macos-tty";
    const fs::path helper = CopyProbe(root, "tty.mode");
    std::string err;
    modelnet::SwarmConfineResult result;
    WithPtyStdin([&] { result = modelnet::SwarmRunConfined(BaseRequest(helper, root), err); });
    const std::string out = ProbeOutput(root);
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINED");
    BOOST_CHECK(out.find("tiocsti_errno=1\n") != std::string::npos);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
