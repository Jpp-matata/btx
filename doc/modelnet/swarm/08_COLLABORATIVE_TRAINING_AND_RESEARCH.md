# BTX Swarm 0.35 — Collaborative training and research

Document: **BTX-SWARM-SPEC-08**, revision **0.1.0**, 19 October 2026.

**Implementation scope:** Real local SGD, full-state checkpoint/resume, two-process DDP, compatible cohort aggregation and grouped REINFORCE are implemented; large-model pretraining frameworks and advanced asynchronous algorithms remain required adapters.

## Training profiles and exact identity

**TRN-001.** Every run binds exact input checkpoint, recipe, model/optimizer configuration, admitted dataset artifacts, data order/cursor, random state, software environment, precision and contribution authority. A final weight hash alone is not a resumable training state. A change to any bound training component creates a new run or an explicit migration/derivation record.

The reference decoder is a real causal PyTorch model, deliberately bounded below 100 million approximate parameters. Its purpose is portable correctness and lifecycle qualification. It is not advertised as a new frontier model or a replacement for production serving kernels.

`Trainer` performs actual gradient updates with SGD momentum, finite checks and clipping. Training-state SafeTensors include model parameters, momentum buffers, sampling RNG, cursor, step and observed token count. Restore reconstructs the optimizer state without pickle. SafeTensors headers are canonicalized for deterministic identity; exact uninterrupted versus resumed training is tested on the same qualified CPU/software profile.

Cross-hardware floating-point execution is not generally bit-identical. The target Lab bridge must distinguish byte-exact replay, numerically bounded recomputation and independently reproducible quality. Do not weaken an existing native strict-admission requirement by calling nondeterministic output “replayed.”

## Fast local training islands

**TRN-002.** Tightly coupled collectives belong inside a measured high-bandwidth island. `btxmesh.island` is an actual `torchrun` entrypoint using distributed data parallelism: Gloo on CPU and a selectable NCCL/CUDA path. Each rank consumes its own seeded samples, participates in real gradient all-reduce, and writes rank-specific training state. The test launches two real Python processes and checks identical synchronized output checkpoints and nontrivial updates.

The NCCL path has code but no GPU execution evidence. Multi-host authenticated rendezvous, FSDP/ZeRO state sharding, parallel model architectures, accelerator-specific kernels, node loss recovery and optimizer-state elasticity are not implemented as qualified production profiles. Private collective ports must not be exposed to unauthenticated public peers.

## Cooperative outer rounds

**TRN-003.** A `TrainingCohort` pins family, current base checkpoint, recipe, eligible island identities, minimum island count, contribution token ceiling, clipping policy and round. A contribution must reference a COMPLETE matching worker job, the current base/recipe, exact worker generation, output artifact and measured token count. Contributions from a different round or incompatible job are rejected.

`aggregate_round` retrieves the actual SafeTensors updates, validates tensor schema/finite values, clips contributions, weights compatible updates by approved token counts, applies an outer momentum step, and persists both new weights and outer optimizer state. It uploads the new artifacts before advancing the round with exact accepted update IDs. A two-worker network test executes this path, rather than passing fabricated vectors into an unconnected API.

This is compatible local-SGD aggregation. It is **not** an implementation or reproduction of Decoupled DiLoCo. It does not safely average unrelated fine-tunes, arbitrary architectures, inconsistent tokenizers or independently initialized adapter bases. Incompatible branches remain forks or require separately evaluated merge/distillation experiments.

## Wide-area asynchronous training contract

The target profile should allow islands to perform multiple local steps between bounded global exchanges, overlap communication, tolerate limited staleness and survive individual island failure. The recipe must declare optimizer ownership, update weighting, staleness limits, compression/quantization error feedback, clipping, accepted membership changes and convergence metrics. A stale worker must not overwrite a newer baseline by presenting a valid hash for an old update.

A production integration with Hivemind, DiLoCo-derived systems or another chosen framework must preserve its actual algorithm, not merely borrow the name. Pin the upstream revision and record supported topology, tested model size, update volume and time-to-quality. WAN parameter exchange must not be mistaken for low-latency expert dispatch on each token.

## Experience generation and reinforcement learning

**TRN-004.** Rollout workers use an exact frozen checkpoint and produce bounded trajectories under explicit data consent. Independent tests/verifiers provide rewards. Reward generation is not automatically trusted because it came from the same model that produced the answer.

The delivered trajectory store binds checkpoint/group, consented visibility, prompt/completion lengths and reward scale; grouped REINFORCE recomputes log probabilities with gradients, centers/scales group rewards, rejects off-policy checkpoint mixing and skips zero-variance groups. It is a real policy-gradient operation, not PPO or GRPO. Full distributed experience ingestion, reward-server isolation, off-policy correction, KL/reference models and adaptive learner scheduling remain required for a production RL profile.

**TRN-005.** Private prompts and proprietary source are not automatically training examples. A trajectory requires explicit dataset permission and derivative-release policy. A model update can leak information even when raw data stays local. Differential privacy, secure aggregation or confidential execution require explicit mechanisms and measured privacy/utility bounds; none is implied by encrypted transport.

## Research diversity and reuse

Model work may include candidate generation, adapter specialization, recipe search, distillation, data curation, independent replication, negative experiments and challenger generation. Research records preserve failed results and source provenance. Sharing findings and tool results can improve the community without changing base weights; the four timescales of knowledge, strategies, specializations and new generations remain distinct processes.

A challenger task must be valid, informative and independently verifiable. Rewarding only hard or unsolved tasks can degrade training. Synthetic tasks need a generator identity, generator policy and independent validity checks before entering fitness measurements.

## Required training qualification

For each profile: check full-state resume, cancelled collective cleanup, duplicate/stale updates, corrupt shards, NaNs, dropout, heterogeneous steps, changed datasets, authority revocation before release and failed aggregation recovery. Report time to a predefined quality threshold and total compute/communication, not just optimizer steps per second. The included synthetic training tests demonstrate working mechanics, not a claim of competitive model intelligence.
