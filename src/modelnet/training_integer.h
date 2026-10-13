// Copyright (c) 2026 The BTX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_MODELNET_TRAINING_INTEGER_H
#define BITCOIN_MODELNET_TRAINING_INTEGER_H

#include <modelnet/training_evidence.h>

#include <cstdint>
#include <string>
#include <vector>

namespace modelnet {

/** Deterministic integer linear profile. This is not the EXACT_CHECKS byte fixture. */
inline constexpr char INTEGER_PROFILE[] = "INTEGER_LINEAR";

struct IntegerExample {
    int32_t x{0};
    int32_t y{0};
};

/**
 * Full trainer state. state_hash covers the weights, both momentum terms,
 * the data cursor, and the step index. A file hash of the final weights
 * alone is not this commitment.
 */
struct IntegerState {
    int64_t weight{0};
    int64_t bias{0};
    int64_t momentum_w{0};
    int64_t momentum_b{0};
    uint64_t cursor{0};
    uint64_t step{0};
    std::string state_hash;
};

struct IntegerRun {
    bool ok{false};
    std::string code;
    IntegerState state;
    std::vector<Interval> intervals;
};

/** Eight frozen pairs. The trainer never fetches a name for them. */
const std::vector<IntegerExample>& IntegerDataset();

/** SHA-384 of the canonical state line. */
std::string IntegerStateHash(const IntegerState& state);

class IntegerTrainer {
public:
    IntegerTrainer() = default;

    const IntegerState& State() const { return m_state; }
    const std::vector<Interval>& Intervals() const { return m_intervals; }

    /** One committed step. The first call starts from the zero state. */
    bool Step(std::string& err);

    /** Run exactly `steps` updates from the current state. */
    bool Run(uint64_t steps, std::string& err);

    /**
     * Continue a committed checkpoint. step == 0 is NOT_RESUMABLE and does
     * not change this object. A checkpoint whose hash does not match a
     * replay of its own step is BTX035_ID_MISMATCH.
     */
    bool Resume(const IntegerState& checkpoint, std::string& err);

    /** Replay from zero and require the same final hash. */
    bool ReplayMatches(const IntegerState& published, std::string& err) const;

    /**
     * Recompute `published.step` updates. A cursor that is not the recomputed
     * cursor is BTX035_RNG. A weight, bias, or momentum mismatch is
     * BTX035_ID_MISMATCH. An empty hash is BTX035_ID_MISMATCH.
     */
    static bool CheckState(const IntegerState& published, std::string& err);

private:
    IntegerState m_state;
    std::vector<Interval> m_intervals;
};

} // namespace modelnet

#endif // BITCOIN_MODELNET_TRAINING_INTEGER_H
