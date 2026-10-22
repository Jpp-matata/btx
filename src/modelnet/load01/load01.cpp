// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/load01/load01.h>
#include <modelnet/nifdt_contract/contract.h>
#include <util/fs.h>

#include <cstdint>
#include <fstream>
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

} // namespace

bool Load01::Load(const std::string& source_path, const std::string& digest_hex, std::string& err)
{
    if (source_path.empty() || digest_hex.empty()) {
        err = "FIELD";
        return false;
    }
    if (m_loaded && source_path != m_source) {
        err = "SOURCE";
        return false;
    }

    const fs::path path = fs::PathFromString(source_path);
    try {
        if (!fs::exists(path)) {
            err = "ABSENT";
            return false;
        }
        if (fs::is_directory(path) || !fs::is_regular_file(path)) {
            err = "IO";
            return false;
        }
    } catch (const fs::filesystem_error&) {
        err = "IO";
        return false;
    }

    std::ifstream in{path, std::ios::binary};
    if (!in) {
        err = "IO";
        return false;
    }

    std::uint64_t hash = kFnvOffset;
    int64_t nbytes = 0;
    char buf[4096];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        const std::streamsize n = in.gcount();
        if (n < 0 || in.bad()) {
            err = "IO";
            return false;
        }
        MixBytes(hash, buf, n);
        nbytes += static_cast<int64_t>(n);
    }
    if (in.bad()) {
        err = "IO";
        return false;
    }

    if (Hex64(hash) != digest_hex) {
        err = "DIGEST";
        return false;
    }

    m_bytes = nbytes;
    m_source = source_path;
    m_loaded = true;
    err.clear();
    return true;
}

} // namespace modelnet
