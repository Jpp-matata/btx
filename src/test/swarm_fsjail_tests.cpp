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
    const fs::path root = fs::temp_directory_path() / "btx-swarm-fsjail-run";
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

#if defined(__APPLE__) && defined(BTX_SWARM_JAIL_PROBE)
#include <sys/ipc.h>
#include <sys/shm.h>

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

fs::path CopyProbe(const fs::path& root)
{
    fs::remove_all(root);
    fs::create_directories(root);
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

// macOS: kernel sandbox instead of a mount namespace. The helper runs and
// writes inside root, with root as its working directory, but cannot open
// the directory above root or /private/etc/hosts, and cannot bind a
// loopback socket.
BOOST_AUTO_TEST_CASE(macos_helper_runs_jailed)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-fsjail-macos";
    const fs::path helper = CopyProbe(root);
    // A System V segment of this process: the jailed helper must not attach.
    const int sysv = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    BOOST_REQUIRE(sysv >= 0);
    {
        std::ofstream id_file{root / "sysv.id"};
        id_file << sysv;
    }
    std::string err = "stale";
    const modelnet::SwarmFilesystemJailResult result = modelnet::SwarmRunFilesystemJail(BaseRequest(helper, root), err);
    const std::string out = ProbeOutput(root);
    shmctl(sysv, IPC_RMID, nullptr);
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "JAILED");
    BOOST_CHECK(result.launched && result.jailed && err.empty());
    BOOST_CHECK(out.find("outside_dir=0\n") != std::string::npos);
    BOOST_CHECK(out.find("etc_hosts=0\n") != std::string::npos);
    BOOST_CHECK(out.find("loopback_bind=0\n") != std::string::npos);
    BOOST_CHECK(out.find("mach_lookup=0\n") != std::string::npos);
    BOOST_CHECK(out.find("hardware_uuid=0\n") != std::string::npos);
    // No fork: the jail sets no rlimits, so this is its only process-count bound.
    BOOST_CHECK(out.find("fork=0\n") != std::string::npos);
    BOOST_CHECK(out.find("signal_parent=0\n") != std::string::npos);
    BOOST_CHECK(out.find("cwd_root=1\n") != std::string::npos);
    BOOST_CHECK(out.find("posix_shm=0\n") != std::string::npos);
    BOOST_CHECK(out.find("sysv_attach=0\n") != std::string::npos);
}

// A helper inside root that is a symlink to a binary outside root is not
// exec'd: the sandbox checks the resolved path.
BOOST_AUTO_TEST_CASE(macos_symlink_out_of_root_refused)
{
    const fs::path outside = fs::temp_directory_path() / "btx-swarm-fsjail-macos-outside";
    const fs::path target = CopyProbe(outside);
    const fs::path root = fs::temp_directory_path() / "btx-swarm-fsjail-macos-link";
    fs::remove_all(root);
    fs::create_directories(root);
    fs::create_symlink(target, root / "probe");
    std::string err;
    const modelnet::SwarmFilesystemJailResult result =
        modelnet::SwarmRunFilesystemJail(BaseRequest(root / "probe", root), err);
    const bool ran = fs::exists(outside / "probe.out");
    fs::remove_all(root);
    fs::remove_all(outside);
    BOOST_CHECK_EQUAL(result.code, "JAIL_ABSENT");
    BOOST_CHECK(!result.launched && !result.jailed);
    BOOST_CHECK(!ran);
}

// A shell script cannot run: its interpreter is outside root.
BOOST_AUTO_TEST_CASE(macos_interpreter_outside_root_refused)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-fsjail-macos-sh";
    fs::remove_all(root);
    fs::create_directories(root);
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

// As in swarm_confine_tests: TIOCSTI on an inherited pty is refused by the
// sandbox (EPERM), not only by the tty driver (EACCES).
BOOST_AUTO_TEST_CASE(macos_terminal_ioctl_denied)
{
    const fs::path root = fs::temp_directory_path() / "btx-swarm-fsjail-macos-tty";
    const fs::path helper = CopyProbe(root);
    std::ofstream{root / "tty.mode"};
    std::string err;
    modelnet::SwarmFilesystemJailResult result;
    WithPtyStdin([&] { result = modelnet::SwarmRunFilesystemJail(BaseRequest(helper, root), err); });
    const std::string out = ProbeOutput(root);
    fs::remove_all(root);
    BOOST_CHECK_EQUAL(result.code, "JAILED");
    BOOST_CHECK(out.find("tiocsti_errno=1\n") != std::string::npos);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
