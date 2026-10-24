// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_confine.h>
#include <test/util/setup_common.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <sys/stat.h>
#include <sys/wait.h>

#include <chrono>
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

#ifndef _WIN32
namespace {

// Resolved: InsideRoot refuses symlinks, and macOS TMPDIR is below /var.
fs::path FreshRoot(const char* name)
{
    const fs::path root = fs::canonical(fs::temp_directory_path()) / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

} // namespace
#endif

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

#if defined(__linux__) || defined(__APPLE__)
// Windows refuses a zero max_tree_bytes in its own sandbox (WINDOWS_SANDBOX_POLICY).
BOOST_AUTO_TEST_CASE(zero_tree_limit)
{
    const fs::path root = FreshRoot("btx-swarm-confine-zero-tree");
    modelnet::SwarmConfineRequest request = BaseRequest(WriteRegularFile(root / "helper.sh"), root);
    request.max_tree_bytes = 0;
    ExpectRefusal(request, "LIMIT");
    fs::remove_all(root);
}
#endif

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
    // canonical: InsideRoot refuses symlinks, and macOS TMPDIR is below /var.
    const fs::path root = fs::canonical(fs::temp_directory_path()) / "btx-swarm-confine-run";
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

// The Seatbelt profile matches resolved paths and InsideRoot refuses
// symlinks; TMPDIR on macOS is below the /var symlink.
fs::path MacRoot(const char* name)
{
    const fs::path root = fs::canonical(fs::temp_directory_path()) / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

fs::path CopyProbe(const fs::path& root, const char* marker)
{
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

// macOS: default-deny Seatbelt instead of namespaces. The helper runs, sees
// the CPU and descriptor limits, and can neither bind a loopback socket nor
// read outside root.
BOOST_AUTO_TEST_CASE(macos_helper_runs_confined)
{
    const fs::path root = MacRoot("btx-swarm-confine-macos");
    modelnet::SwarmConfineRequest request = BaseRequest(CopyProbe(root, nullptr), root);
    request.max_address_bytes = 1LL << 30;
    std::string err = "stale";
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    const std::string out = ProbeOutput(root);
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINED");
    BOOST_CHECK(result.launched && result.limits_applied && err.empty());
    BOOST_CHECK(WIFEXITED(result.child_status));
    BOOST_CHECK_EQUAL(WEXITSTATUS(result.child_status), 0);
    BOOST_CHECK(out.find("outside_dir=0\n") != std::string::npos);
    BOOST_CHECK(out.find("etc_hosts=0\n") != std::string::npos);
    BOOST_CHECK(out.find("loopback_bind=0\n") != std::string::npos);
    BOOST_CHECK(out.find("nofile=64\n") != std::string::npos);
    BOOST_CHECK(out.find("cpu=30\n") != std::string::npos);
    BOOST_CHECK(out.find("fsize=1073741824\n") != std::string::npos);
    // Regression checks for the default-deny profile.
    for (const char* line : {"mach_lookup=0\n", "fork=0\n", "signal_parent=0\n", "posix_shm=0\n", "cwd_root=1\n"}) {
        BOOST_CHECK_MESSAGE(out.find(line) != std::string::npos, "probe line " << line);
    }
}

// macOS refuses RLIMIT_AS; the parent kills a child whose footprint passes
// max_address_bytes. A signal reads as not executed, so the run is refused:
// the probe started but did not finish its 4 GiB.
BOOST_AUTO_TEST_CASE(macos_address_limit_enforced)
{
    const fs::path root = MacRoot("btx-swarm-confine-macos-alloc");
    modelnet::SwarmConfineRequest request = BaseRequest(CopyProbe(root, "alloc.mode"), root);
    request.max_address_bytes = 256LL << 20;
    std::string err;
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    const bool started = fs::exists(root / "alloc.started");
    const bool done = fs::exists(root / "alloc.done");
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINEMENT_ABSENT");
    BOOST_CHECK(!result.launched);
    BOOST_CHECK(started);
    BOOST_CHECK(!done);
}

// RLIMIT_CPU is enforced by the kernel (SIGXCPU): the probe started but did
// not finish its 20 s of CPU.
BOOST_AUTO_TEST_CASE(macos_cpu_limit_enforced)
{
    const fs::path root = MacRoot("btx-swarm-confine-macos-spin");
    modelnet::SwarmConfineRequest request = BaseRequest(CopyProbe(root, "spin.mode"), root);
    request.max_cpu_seconds = 1;
    std::string err;
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    const bool started = fs::exists(root / "spin.started");
    const bool done = fs::exists(root / "spin.done");
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINEMENT_ABSENT");
    BOOST_CHECK(!result.launched);
    BOOST_CHECK(started);
    BOOST_CHECK(!done);
}

// macOS only sends SIGXCPU at RLIMIT_CPU. A helper that ignores it is killed
// by the parent 250 ms past the limit, long before its 20 s spin ends.
BOOST_AUTO_TEST_CASE(macos_cpu_limit_holds_when_sigxcpu_ignored)
{
    const fs::path root = MacRoot("btx-swarm-confine-macos-spin-ignore");
    modelnet::SwarmConfineRequest request = BaseRequest(CopyProbe(root, "spin_ignore.mode"), root);
    request.max_cpu_seconds = 1;
    std::string err;
    const auto start = std::chrono::steady_clock::now();
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
    const bool started = fs::exists(root / "spin.started");
    const bool done = fs::exists(root / "spin.done");
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINEMENT_ABSENT");
    BOOST_CHECK(!result.launched);
    BOOST_CHECK(started);
    BOOST_CHECK(!done);
    BOOST_CHECK_LT(seconds, 10);
}

namespace {
void WriteBytes(const fs::path& path, size_t bytes)
{
    std::ofstream out{path, std::ios::binary};
    out << std::string(bytes, 'x');
    BOOST_REQUIRE(out);
}

uintmax_t FileBytes(const fs::path& path)
{
    return fs::exists(path) ? fs::file_size(path) : 0;
}
} // namespace

// A root that is already over max_tree_bytes is refused before launch.
BOOST_AUTO_TEST_CASE(macos_tree_limit_refuses_root_before_launch)
{
    const fs::path root = MacRoot("btx-swarm-confine-macos-tree-before");
    modelnet::SwarmConfineRequest request = BaseRequest(CopyProbe(root, nullptr), root);
    request.max_tree_bytes = fs::file_size(request.helper) + 1024;
    WriteBytes(root / "input.bin", 4096);
    ExpectRefusal(request, "TREE_LIMIT");
    const bool ran = fs::exists(root / "probe.out");
    fs::remove_all(root);
    BOOST_CHECK(!ran);
}

// RLIMIT_FSIZE stops one large file during the run: SIGXFSZ kills the probe
// (a signal reads as not executed), and the file never passes max_tree_bytes.
BOOST_AUTO_TEST_CASE(macos_file_size_limit_enforced)
{
    const fs::path root = MacRoot("btx-swarm-confine-macos-fill");
    modelnet::SwarmConfineRequest request = BaseRequest(CopyProbe(root, "fill.mode"), root);
    request.max_tree_bytes = fs::file_size(request.helper) + (8 << 20);
    std::string err;
    const modelnet::SwarmConfineResult result = modelnet::SwarmRunConfined(request, err);
    const bool started = fs::exists(root / "fill.started");
    const bool done = fs::exists(root / "fill.done");
    const uintmax_t written = FileBytes(root / "fill.bin");
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINEMENT_ABSENT");
    BOOST_CHECK(!result.launched);
    BOOST_CHECK(started);
    BOOST_CHECK(!done);
    BOOST_CHECK_LE(written, request.max_tree_bytes);
}

// A helper that ignores SIGXFSZ gets EFBIG and exits normally. The file still
// stops at max_tree_bytes, and the tree check after exit refuses the result.
BOOST_AUTO_TEST_CASE(macos_file_size_limit_holds_when_sigxfsz_ignored)
{
    const fs::path root = MacRoot("btx-swarm-confine-macos-fill-ignore");
    modelnet::SwarmConfineRequest request = BaseRequest(CopyProbe(root, "fill_ignore.mode"), root);
    request.max_tree_bytes = fs::file_size(request.helper) + (8 << 20);
    ExpectRefusal(request, "TREE_LIMIT");
    const bool refused = fs::exists(root / "fill.refused");
    const bool done = fs::exists(root / "fill.done");
    const uintmax_t written = FileBytes(root / "fill.bin");
    fs::remove_all(root);
    BOOST_CHECK(refused);
    BOOST_CHECK(!done);
    BOOST_CHECK_GT(written, 0U);
    BOOST_CHECK_LE(written, request.max_tree_bytes);
}

// Files that are each below RLIMIT_FSIZE but together above max_tree_bytes:
// the probe runs to the end, and the tree check after exit refuses the result.
BOOST_AUTO_TEST_CASE(macos_tree_limit_refuses_result_after_run)
{
    const fs::path root = MacRoot("btx-swarm-confine-macos-spread");
    modelnet::SwarmConfineRequest request = BaseRequest(CopyProbe(root, "spread.mode"), root);
    request.max_tree_bytes = fs::file_size(request.helper) + (4 << 20);
    ExpectRefusal(request, "TREE_LIMIT");
    const bool done = fs::exists(root / "spread.done");
    fs::remove_all(root);
    BOOST_CHECK(done);
}
#endif

#ifdef __linux__
// A root that is already over max_tree_bytes is refused before launch.
BOOST_AUTO_TEST_CASE(linux_tree_limit_refuses_root_before_launch)
{
    const fs::path root = FreshRoot("btx-swarm-confine-linux-tree-before");
    const fs::path helper = root / "helper.sh";
    {
        std::ofstream out{helper};
        out << "#!/bin/sh\ntouch ran\n";
        BOOST_REQUIRE(out);
        std::ofstream extra{root / "input.bin", std::ios::binary};
        extra << std::string(4096, 'x');
        BOOST_REQUIRE(extra);
    }
    BOOST_REQUIRE(chmod(helper.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0);
    modelnet::SwarmConfineRequest request = BaseRequest(helper, root);
    request.max_tree_bytes = fs::file_size(helper) + 1024;
    ExpectRefusal(request, "TREE_LIMIT");
    const bool ran = fs::exists(root / "ran");
    fs::remove_all(root);
    BOOST_CHECK(!ran);
}

#ifdef BTX_SWARM_LINUX_PROBE
namespace {

fs::path CopyProbe(const fs::path& root, const char* marker)
{
    const fs::path helper = root / "probe";
    fs::copy_file(fs::path{BTX_SWARM_LINUX_PROBE}, helper, fs::copy_options::overwrite_existing);
    BOOST_REQUIRE(chmod(helper.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0);
    if (marker != nullptr) WriteRegularFile(root / marker);
    return helper;
}

// The cases below need the helper to run. Without user namespaces it never
// starts, and they have nothing to measure.
bool ConfinementAvailable()
{
    const fs::path root = FreshRoot("btx-swarm-confine-linux-available");
    std::string err;
    const auto result = modelnet::SwarmRunConfined(BaseRequest(CopyProbe(root, nullptr), root), err);
    fs::remove_all(root);
    if (result.code != "CONFINED") BOOST_TEST_MESSAGE("Linux confinement absent here: " << result.code);
    return result.code == "CONFINED";
}

} // namespace

// The child sets RLIMIT_FSIZE to max_tree_bytes before exec.
BOOST_AUTO_TEST_CASE(linux_file_size_limit_applied)
{
    if (!ConfinementAvailable()) return;
    const fs::path root = FreshRoot("btx-swarm-confine-linux-fsize");
    modelnet::SwarmConfineRequest request = BaseRequest(CopyProbe(root, nullptr), root);
    request.max_tree_bytes = 64 << 20;
    std::string err;
    const auto result = modelnet::SwarmRunConfined(request, err);
    std::ifstream in{root / "probe.out"};
    const std::string out{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "CONFINED");
    BOOST_CHECK_EQUAL(out, "fsize=67108864\n");
}

// Files that are each below RLIMIT_FSIZE but together above max_tree_bytes:
// the probe runs to the end, and the tree check after exit refuses the result.
BOOST_AUTO_TEST_CASE(linux_tree_limit_refuses_result_after_run)
{
    if (!ConfinementAvailable()) return;
    const fs::path root = FreshRoot("btx-swarm-confine-linux-burst");
    modelnet::SwarmConfineRequest request = BaseRequest(CopyProbe(root, "burst.mode"), root);
    request.max_tree_bytes = fs::file_size(request.helper) + (2 << 20);
    ExpectRefusal(request, "TREE_LIMIT");
    const bool done = fs::exists(root / "burst.done");
    fs::remove_all(root);
    BOOST_CHECK(done);
}
#endif // BTX_SWARM_LINUX_PROBE
#endif // __linux__

BOOST_AUTO_TEST_SUITE_END()
