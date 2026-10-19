# BTX Swarm 0.35 — Artifact and shared KV fabric

Document: **BTX-SWARM-SPEC-07**, revision **0.1.0**, 19 October 2026.

**Implementation scope:** Encrypted bounded CAS, actual paged reference KV transfer and exact reuse are implemented and network-tested. Production GPU zero-copy, NIXL/RDMA and approximate cross-model reuse are not implemented.

## Object transport and storage

**KV-001.** Durable artifacts are exact immutable byte objects, divided into bounded 4 MiB pieces with SHA-384 verification. Manifests bind artifact identity, length and ordered piece identities. Transfer may resume from verified pieces; a manifest is not usable until required pieces have been verified. Paths from peers are never storage keys or shell arguments.

The portable CAS encrypts pieces under realm keys using AES-GCM and realm-scoped authenticated storage names. It validates reads, records pins and physical leases, refuses quota exhaustion conservatively and atomically commits indexes. An eviction plan must be valid before any referenced piece is removed. Index changes commit before orphan file deletion so a failed write cannot delete bytes still represented as valid.

**KV-002.** Cross-realm plaintext deduplication must not reveal membership or content equality. An untrusted storage source can be used for authenticated ciphertext, but the producer's correctness is a separate trust decision. The current store is local to the sidecar or worker, not a native ModelNet CAS replacement. Native acquisition and retention must be reconciled through an explicit bridge.

Current flat manifests allow at most 16,384 pieces, corresponding to 64 GiB at full piece size. Large frontier checkpoints, optimizer states and datasets need hierarchical manifests, streaming indexing and bounded metadata roots. Do not claim support for arbitrarily large models merely because payload pieces are individually small.

## Exact KV compatibility

**KV-003.** A compatible KV handle binds exact checkpoint, architecture, tokenizer, ordered adapters, runtime build, dtype, layout, position encoding, attention mask, quantization profile and tenant epoch. It also binds the exact token-prefix identity, page ranges, layers, KV-head geometry, lengths, producer and expiry. Sharing a family ID is not sufficient.

The reference layout is `[batch, heads, tokens, head_dimension]` for each layer's K and V. Prefix identities are keyed in the realm; no private plaintext prefix digest is published globally. Pages are complete immutable tensors. Suffix branches use copy-on-write semantics: an existing prefix's bytes do not change when a new continuation appends tokens.

**KV-004.** Appending a causal token does not invalidate existing prefix KV. Altering an earlier token or any compatibility-bound model parameter invalidates dependent state. The consumer must either recompute or use a specifically validated transformation; it may not reinterpret incompatible tensors as memory.

The implementation transfers actual SafeTensors KV pages over authenticated network endpoints and validates geometry before use. Two separately identified workers in the network test generate identical completion bytes while the second reuses fifteen of sixteen input-prefix tokens. The final input token is recomputed when needed because the portable handle does not include trusted final logits. This verifies reuse correctness; it is not a throughput benchmark.

## Producer assurance and privacy

A content hash proves byte identity, not that those bytes were computed from the promised weights and prefix. A malicious producer can publish a self-consistent poisoned tensor. The current worker only accepts exact caches from explicitly trusted producer identities and identifies locator assurance as producer-asserted, not compute-proven. A model author's signature does not by itself authorize executable compiler caches or runtime software.

**KV-005.** Cache pages do not replace Lab replay/admission, work evidence, a fresh execution grant or independent evaluation. A cache is disposable. Missing, expired, corrupted or incompatible pages cause recomputation, not reduced admission assurance.

Treat KV as potentially prompt-revealing data. Prevent cross-tenant lookup oracles, normalize unauthorized cache responses and scope encryption/lookup keys. Revocation blocks new accesses but cannot undo already copied tensors. Production key-epoch rotation, hit/miss timing defenses and endpoint metadata indistinguishability require additional review.

## Placement and prefetch

Compare three alternatives: execute near an already resident compatible cache, transfer needed pages to a suitable runtime, or recompute locally. Include queueing, transfer setup, useful bandwidth, missing page fraction, memory pressure, CPU/GPU copies and expected continuation probability. Prefer an uncached idle worker over an overloaded cached worker when total completion time is lower.

A model-independent cost estimate for conventional attention KV is `2 × layers × KV_heads × head_dim × prefix_tokens × bytes_per_element`, excluding replication, allocator padding and quantization metadata. It is a sizing tool, not a performance result. Do not multiply unrelated published speedups together.

Work graphs should provide next-use hints: retain a parent context while a tool runs, prefetch for known children, demote inactive branches, and preferentially retain shared ancestors. Runtime allocators retain authority over actual memory residency. Current code includes exact reuse and placement helpers; full engine-integrated workflow prefetch is an outstanding adapter requirement.

## Production acceleration profiles

| Profile | Required integration | Current delivery |
|---|---|---|
| Exact local/shared prefix | Runtime-compatible pages, complete fingerprint | Executed on reference PyTorch decoder |
| vLLM / SGLang prefix and host tiers | Version-pinned launch/config and live output checks | Generated plans and process/client code; not executed with those engines |
| LMCache distributed tier | Qualified current connector/backend contract | Plan uses V1-style connector; migration/qualification required |
| NIXL / Mooncake / RDMA | Registered-memory lifetime, connection auth, data ownership, completion fencing | Specification and qualification requirements only |
| LoRA/base decomposition | Proven shared subgraph, adapter ordering and quality checks | Not implemented |
| Cross-model transformed KV | Pair-specific converter plus held-out equivalence/error policy | Not implemented; never silently enabled |
| Compiled/runtime caches | Trusted builder, exact runtime/compiler/shape and source binding | Required production extension, not a signed-model shortcut |

A high-performance feature is automatically eligible only when a compatible, locally authorized and empirically qualified implementation exists. Availability of an importable module is discovery, not qualification.
