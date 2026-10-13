// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <modelnet/training_integer.h>

#include <crypto/sha384.h>

#include <sstream>
#include <string>

namespace modelnet {
namespace {

bool Fail(std::string& err, const char* code)
{
    err = code;
    return false;
}

std::string HexLower(const unsigned char* data, size_t n)
{
    static const char* kHex = "0123456789abcdef";
    std::string out(n * 2, '0');
    for (size_t i = 0; i < n; ++i) {
        out[i * 2] = kHex[data[i] >> 4];
        out[i * 2 + 1] = kHex[data[i] & 0x0f];
    }
    return out;
}

} // namespace

const std::vector<IntegerExample>& IntegerDataset()
{
    static const std::vector<IntegerExample> kData = {
        {1, 2}, {2, 4}, {3, 6}, {4, 8}, {5, 10}, {6, 12}, {7, 14}, {8, 16},
    };
    return kData;
}

std::string IntegerStateHash(const IntegerState& state)
{
    std::ostringstream text;
    text << INTEGER_PROFILE << '\n'
         << state.weight << '\n'
         << state.bias << '\n'
         << state.momentum_w << '\n'
         << state.momentum_b << '\n'
         << state.cursor << '\n'
         << state.step;
    const std::string bytes = text.str();
    unsigned char hash[CSHA384::OUTPUT_SIZE];
    CSHA384().Write(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()).Finalize(hash);
    return HexLower(hash, CSHA384::OUTPUT_SIZE);
}

bool IntegerTrainer::Step(std::string& err)
{
    const std::vector<IntegerExample>& data = IntegerDataset();
    if (data.empty()) return Fail(err, "BTX035_PROFILE");
    const std::string start_hash = IntegerStateHash(m_state);
    const IntegerExample& sample = data[m_state.cursor % data.size()];
    const int64_t prediction = m_state.weight * static_cast<int64_t>(sample.x) + m_state.bias;
    const int64_t residual = prediction - static_cast<int64_t>(sample.y);
    m_state.momentum_w = m_state.momentum_w / 2 + residual * static_cast<int64_t>(sample.x);
    m_state.momentum_b = m_state.momentum_b / 2 + residual;
    m_state.weight -= m_state.momentum_w / 8;
    m_state.bias -= m_state.momentum_b / 8;
    ++m_state.cursor;
    ++m_state.step;
    m_state.state_hash = IntegerStateHash(m_state);

    Interval interval;
    interval.start_inclusive = m_state.step - 1;
    interval.end_exclusive = m_state.step;
    interval.start_state = start_hash;
    interval.end_state = m_state.state_hash;
    interval.profile = INTEGER_PROFILE;
    interval.rng = "none";
    interval.optimizer = "integer-momentum";
    interval.data_cursor = std::to_string(m_state.cursor);
    interval.ancestry.clear();
    interval.publisher_only = false;
    m_intervals.push_back(std::move(interval));
    err.clear();
    return true;
}

bool IntegerTrainer::Run(uint64_t steps, std::string& err)
{
    if (steps == 0) return Fail(err, "BTX035_PROFILE");
    for (uint64_t i = 0; i < steps; ++i) {
        if (!Step(err)) return false;
    }
    return true;
}

bool IntegerTrainer::Resume(const IntegerState& checkpoint, std::string& err)
{
    if (checkpoint.step == 0) return Fail(err, "BTX035_NOT_RESUMABLE");
    std::string check_err;
    if (!CheckState(checkpoint, check_err)) return Fail(err, check_err.c_str());
    m_state = checkpoint;
    m_state.state_hash = IntegerStateHash(m_state);
    m_intervals.clear();
    err.clear();
    return true;
}

bool IntegerTrainer::ReplayMatches(const IntegerState& published, std::string& err) const
{
    return CheckState(published, err);
}

bool IntegerTrainer::CheckState(const IntegerState& published, std::string& err)
{
    if (published.state_hash.empty() || published.step == 0) return Fail(err, "BTX035_ID_MISMATCH");
    IntegerTrainer replay;
    if (!replay.Run(published.step, err)) return false;
    const IntegerState& got = replay.State();
    if (got.cursor != published.cursor) return Fail(err, "BTX035_RNG");
    if (got.weight != published.weight || got.bias != published.bias ||
        got.momentum_w != published.momentum_w || got.momentum_b != published.momentum_b ||
        got.state_hash != published.state_hash) {
        return Fail(err, "BTX035_ID_MISMATCH");
    }
    err.clear();
    return true;
}

} // namespace modelnet
