// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/cap02/cap02.h>
#include <modelnet/nifdt_contract/contract.h>
#include <util/fs.h>

#include <cstdint>
#include <fstream>
#include <limits>
#include <string>

namespace modelnet {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void MixBytes(std::uint64_t& hash, const char* data, std::streamsize n)
{
    for (std::streamsize i = 0; i < n; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= kFnvPrime;
    }
}

std::string Hex64(std::uint64_t hash)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 0; i < 16; ++i) {
        const int shift = (15 - i) * 4;
        out[static_cast<std::size_t>(i)] = kHex[(hash >> shift) & 0xF];
    }
    return out;
}

bool IsHex16(const std::string& hex)
{
    if (hex.size() != 16) {
        return false;
    }
    for (unsigned char c : hex) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool SingleComponent(const std::string& name)
{
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    const fs::path name_path = fs::PathFromString(name);
    if (name_path.has_parent_path() || name_path.is_absolute()) {
        return false;
    }
    return fs::PathToString(name_path.filename()) == name;
}

bool FileDigest(const fs::path& file, std::string& hex)
{
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return false;
    }
    std::uint64_t hash = kFnvOffset;
    char buf[4096];
    while (true) {
        in.read(buf, sizeof(buf));
        const std::streamsize n = in.gcount();
        if (n > 0) {
            MixBytes(hash, buf, n);
        }
        if (in.bad()) {
            return false;
        }
        if (!in) {
            break;
        }
    }
    hex = Hex64(hash);
    return true;
}

bool LineMatches(const fs::path& root, const std::string& line)
{
    const std::size_t sp = line.find(' ');
    if (sp == std::string::npos || line.find(' ', sp + 1) != std::string::npos) {
        return false;
    }
    const std::string name = line.substr(0, sp);
    const std::string expect = line.substr(sp + 1);
    if (!SingleComponent(name) || !IsHex16(expect)) {
        return false;
    }
    const fs::path child = root / fs::PathFromString(name);
    try {
        if (!fs::exists(child) || !fs::is_regular_file(child)) {
            return false;
        }
    } catch (const fs::filesystem_error&) {
        return false;
    }
    std::string got;
    if (!FileDigest(child, got)) {
        return false;
    }
    return got == expect;
}

} // namespace

bool Cap02::Qualify(const std::string& dir, std::string& err)
{
    m_files = 0;
    if (dir.empty()) {
        err = "FIELD";
        return false;
    }

    const fs::path root = fs::PathFromString(dir);
    fs::path manifest;
    try {
        if (!fs::exists(root) || !fs::is_directory(root)) {
            err = "ABSENT";
            return false;
        }
        manifest = root / "manifest.txt";
        if (!fs::exists(manifest) || !fs::is_regular_file(manifest)) {
            err = "ABSENT";
            return false;
        }
    } catch (const fs::filesystem_error&) {
        err = "ABSENT";
        return false;
    }

    std::ifstream in{manifest};
    if (!in) {
        err = "ABSENT";
        return false;
    }

    int count = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (!LineMatches(root, line)) {
            err = "DIGEST";
            return false;
        }
        if (count == std::numeric_limits<int>::max()) {
            err = "DIGEST";
            return false;
        }
        ++count;
    }
    if (in.bad() || !in.eof()) {
        err = "DIGEST";
        return false;
    }
    if (count < 1) {
        err = "DIGEST";
        return false;
    }
    m_files = count;
    err.clear();
    return true;
}

} // namespace modelnet
