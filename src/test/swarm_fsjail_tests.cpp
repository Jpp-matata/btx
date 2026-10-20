// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/swarm_fsjail.h>
#include <test/util/setup_common.h>
#include <util/fs.h>

#include <boost/test/unit_test.hpp>

#include <sys/stat.h>

#include <fstream>
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

BOOST_AUTO_TEST_SUITE_END()
