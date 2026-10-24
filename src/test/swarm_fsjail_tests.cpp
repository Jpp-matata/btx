// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_fsjail.h>
#include <test/util/setup_common.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <sys/stat.h>

#include <fstream>
#include <iterator>
#include <string>

namespace {

modelnet::SwarmFilesystemJailRequest BaseRequest(const fs::path& helper, const fs::path& root)
{
    modelnet::SwarmFilesystemJailRequest request;
    request.helper = helper;
    request.root = root;
    request.network_denied = true;
    return request;
}

void ExpectRefusal(const modelnet::SwarmFilesystemJailRequest& request, const char* code)
{
    std::string err = "stale";
    const modelnet::SwarmFilesystemJailResult result = modelnet::SwarmRunFilesystemJail(request, err);
    BOOST_CHECK(!result.launched);
    BOOST_CHECK(!result.jailed);
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

BOOST_FIXTURE_TEST_SUITE(swarm_fsjail_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(empty_helper_or_root)
{
    ExpectRefusal(BaseRequest(fs::path{}, fs::temp_directory_path()), "JAIL");
    const fs::path helper = WriteRegularFile(fs::temp_directory_path() / "btx-swarm-fsjail-empty-helper");
    ExpectRefusal(BaseRequest(helper, fs::path{}), "JAIL");
    fs::remove(helper);
}

BOOST_AUTO_TEST_CASE(helper_not_regular_file)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-fsjail-notfile";
    const fs::path directory = root / "directory";
    fs::create_directories(directory);
    ExpectRefusal(BaseRequest(directory, root), "SANDBOX_ABSENT");
    ExpectRefusal(BaseRequest(root / "missing", root), "SANDBOX_ABSENT");
    fs::remove_all(root);
}

BOOST_AUTO_TEST_CASE(network_not_denied)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-fsjail-network";
    const fs::path helper = WriteRegularFile(root / "helper.sh");
    modelnet::SwarmFilesystemJailRequest request = BaseRequest(helper, root);
    request.network_denied = false;
    ExpectRefusal(request, "NETWORK");
    fs::remove_all(root);
}

BOOST_AUTO_TEST_CASE(path_outside_or_dotdot)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-fsjail-path";
    const fs::path helper = WriteRegularFile(root / "helper.sh");
    const fs::path outside_root = fs::temp_directory_path() / "btx-swarm-fsjail-outside";
    const fs::path outside = WriteRegularFile(outside_root / "helper.sh");
    ExpectRefusal(BaseRequest(outside, root), "PATH");

    const fs::path dotted = root / ".." / root.filename() / "helper.sh";
    BOOST_REQUIRE(fs::exists(dotted));
    BOOST_REQUIRE(fs::is_regular_file(dotted));
    ExpectRefusal(BaseRequest(dotted, root), "PATH");

    fs::remove_all(root);
    fs::remove_all(outside_root);
}

BOOST_AUTO_TEST_CASE(jailed_helper_or_namespace_absent)
{
    // canonical: InsideRoot refuses symlinks, and macOS TMPDIR is below /var.
    const fs::path root = fs::canonical(fs::temp_directory_path()) / "btx-swarm-fsjail-run";
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
    const modelnet::SwarmFilesystemJailResult result = modelnet::SwarmRunFilesystemJail(BaseRequest(helper, root), err);
    fs::remove_all(root);

    const bool jailed = result.code == "JAILED" && result.launched && result.jailed && err.empty();
    const bool absent = result.code == "JAIL_ABSENT" && !result.launched && !result.jailed && err == "JAIL_ABSENT";
    BOOST_CHECK(jailed || absent);
    BOOST_CHECK(!(result.launched && result.code == "JAIL_ABSENT"));
    BOOST_CHECK(!(result.jailed && result.code == "JAIL_ABSENT"));
}

BOOST_AUTO_TEST_CASE(zero_tree_limit)
{
    // canonical: InsideRoot refuses symlinks, and macOS TMPDIR is below /var.
    const fs::path root = fs::canonical(fs::temp_directory_path()) / "btx-swarm-fsjail-zero-tree";
    modelnet::SwarmFilesystemJailRequest request = BaseRequest(WriteRegularFile(root / "helper.sh"), root);
    request.max_tree_bytes = 0;
    ExpectRefusal(request, "LIMIT");
    fs::remove_all(root);
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

fs::path CopyProbe(const fs::path& root)
{
    const fs::path helper = root / "probe";
    fs::copy_file(fs::path{BTX_SWARM_JAIL_PROBE}, helper, fs::copy_options::overwrite_existing);
    BOOST_REQUIRE(chmod(helper.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0);
    return helper;
}

std::string ProbeOutput(const fs::path& root)
{
    std::ifstream in{root / "probe.out"};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace

// macOS: default-deny Seatbelt instead of a mount namespace. The helper runs
// and writes inside root, but cannot open the directory above root or
// /private/etc/hosts, and cannot bind a loopback socket.
BOOST_AUTO_TEST_CASE(macos_helper_runs_jailed)
{
    const fs::path root = MacRoot("btx-swarm-fsjail-macos");
    std::string err = "stale";
    const modelnet::SwarmFilesystemJailResult result =
        modelnet::SwarmRunFilesystemJail(BaseRequest(CopyProbe(root), root), err);
    const std::string out = ProbeOutput(root);
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "JAILED");
    BOOST_CHECK(result.launched && result.jailed && err.empty());
    BOOST_CHECK(out.find("outside_dir=0\n") != std::string::npos);
    BOOST_CHECK(out.find("etc_hosts=0\n") != std::string::npos);
    BOOST_CHECK(out.find("loopback_bind=0\n") != std::string::npos);
    // Regression checks for the default-deny profile.
    for (const char* line : {"mach_lookup=0\n", "fork=0\n", "signal_parent=0\n", "posix_shm=0\n", "cwd_root=1\n"}) {
        BOOST_CHECK_MESSAGE(out.find(line) != std::string::npos, "probe line " << line);
    }
}

// A helper inside root that is a symlink to a binary outside root is refused
// before fork.
BOOST_AUTO_TEST_CASE(macos_symlink_out_of_root_refused)
{
    const fs::path outside = MacRoot("btx-swarm-fsjail-macos-outside");
    const fs::path target = CopyProbe(outside);
    const fs::path root = MacRoot("btx-swarm-fsjail-macos-link");
    fs::create_symlink(target, root / "probe");
    std::string err;
    const modelnet::SwarmFilesystemJailResult result =
        modelnet::SwarmRunFilesystemJail(BaseRequest(root / "probe", root), err);
    const bool ran = fs::exists(outside / "probe.out");
    fs::remove_all(root);
    fs::remove_all(outside);
    BOOST_CHECK_EQUAL(result.code, "PATH");
    BOOST_CHECK(!result.launched && !result.jailed);
    BOOST_CHECK(!ran);
}

// A shell script cannot run: its interpreter is outside root.
BOOST_AUTO_TEST_CASE(macos_interpreter_outside_root_refused)
{
    const fs::path root = MacRoot("btx-swarm-fsjail-macos-sh");
    const fs::path helper = root / "helper.sh";
    {
        std::ofstream script{helper};
        script << "#!/bin/sh\necho escaped > " << fs::PathToString(root / "ran") << "\n";
    }
    BOOST_REQUIRE(chmod(helper.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0);
    std::string err;
    const modelnet::SwarmFilesystemJailResult result = modelnet::SwarmRunFilesystemJail(BaseRequest(helper, root), err);
    const bool ran = fs::exists(root / "ran");
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "JAIL_ABSENT");
    BOOST_CHECK(!ran);
}

// The jail passes max_tree_bytes to the macOS confinement: a root over the
// limit is refused before launch, with the same code as on Linux.
BOOST_AUTO_TEST_CASE(macos_tree_limit_refused)
{
    const fs::path root = MacRoot("btx-swarm-fsjail-macos-tree");
    modelnet::SwarmFilesystemJailRequest request = BaseRequest(CopyProbe(root), root);
    request.max_tree_bytes = fs::file_size(request.helper) + 1024;
    {
        std::ofstream extra{root / "input.bin", std::ios::binary};
        extra << std::string(4096, 'x');
    }
    ExpectRefusal(request, "TREE_LIMIT");
    const bool ran = fs::exists(root / "probe.out");
    fs::remove_all(root);
    BOOST_CHECK(!ran);
}
#endif

#ifdef __linux__
namespace {

fs::path FreshRoot(const char* name)
{
    const fs::path root = fs::canonical(fs::temp_directory_path()) / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

} // namespace

// A root that is already over max_tree_bytes is refused before launch.
BOOST_AUTO_TEST_CASE(linux_tree_limit_refused)
{
    const fs::path root = FreshRoot("btx-swarm-fsjail-linux-tree");
    const fs::path helper = WriteRegularFile(root / "helper");
    BOOST_REQUIRE(chmod(helper.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0);
    {
        std::ofstream extra{root / "input.bin", std::ios::binary};
        extra << std::string(4096, 'x');
    }
    modelnet::SwarmFilesystemJailRequest request = BaseRequest(helper, root);
    request.max_tree_bytes = fs::file_size(helper) + 1024;
    ExpectRefusal(request, "TREE_LIMIT");
    fs::remove_all(root);
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

// The cases below need the helper to run. Without user namespaces the jail
// is absent, and they have nothing to measure.
bool JailAvailable()
{
    const fs::path root = FreshRoot("btx-swarm-fsjail-linux-available");
    std::string err;
    const auto result = modelnet::SwarmRunFilesystemJail(BaseRequest(CopyProbe(root, nullptr), root), err);
    fs::remove_all(root);
    if (result.code != "JAILED") BOOST_TEST_MESSAGE("Linux filesystem jail absent here: " << result.code);
    return result.code == "JAILED";
}

} // namespace

// The jailed child sets RLIMIT_FSIZE to max_tree_bytes before exec.
BOOST_AUTO_TEST_CASE(linux_jail_file_size_limit_applied)
{
    if (!JailAvailable()) return;
    const fs::path root = FreshRoot("btx-swarm-fsjail-linux-fsize");
    modelnet::SwarmFilesystemJailRequest request = BaseRequest(CopyProbe(root, nullptr), root);
    request.max_tree_bytes = 64 << 20;
    std::string err;
    const auto result = modelnet::SwarmRunFilesystemJail(request, err);
    std::ifstream in{root / "probe.out"};
    const std::string out{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "JAILED");
    BOOST_CHECK_EQUAL(out, "fsize=67108864\n");
}

// Files that are each below RLIMIT_FSIZE but together above max_tree_bytes:
// the probe runs to the end, and the tree check after exit refuses the result.
BOOST_AUTO_TEST_CASE(linux_jail_tree_limit_refuses_result_after_run)
{
    if (!JailAvailable()) return;
    const fs::path root = FreshRoot("btx-swarm-fsjail-linux-burst");
    modelnet::SwarmFilesystemJailRequest request = BaseRequest(CopyProbe(root, "burst.mode"), root);
    request.max_tree_bytes = fs::file_size(request.helper) + (2 << 20);
    ExpectRefusal(request, "TREE_LIMIT");
    const bool done = fs::exists(root / "burst.done");
    fs::remove_all(root);
    BOOST_CHECK(done);
}
#endif // BTX_SWARM_LINUX_PROBE
#endif // __linux__

BOOST_AUTO_TEST_SUITE_END()
