# BTX Swarm 0.35 — Architecture and trust boundaries

Document: **BTX-SWARM-SPEC-02**, revision **0.1.0**, 19 October 2026.

**Implementation scope:** Separate native launcher, Python control/worker services, durable objects and accounting are implemented; realm sharding and native PIT/PQ1 integration remain required.

## Process model

```text
btxd                         monetary consensus, wallet and UTXO state
  └─ btx-modeld              existing ModelNet lifecycle and verified native artifacts
       └─ btx-swarmd          new default sidecar, own state directory and identity
            ├─ control       signed commands, static-committee Raft, durable metadata
            ├─ discovery     catalog observer + signed LAN metadata
            └─ workers       bounded trusted subprocesses, actual tensors and training
btx-paynetd                  separately started local accounting service
  └─ optional read-only btxd RPC observation; no signing or broadcast capability
```

**ARC-001.** Control, fast tensor movement and durable research artifacts MUST be separate logical planes. No generated token, optimizer microstep, GPU DMA completion or cache hit needs an L1 transaction. No model-plane failure may mutate monetary consensus state.

**ARC-002.** Native integration is an RAII supervisor in `src/modeld.cpp`, plus a child CMake directory under the existing ModelNet build. `Supervisor` launches without a shell, passes an environment allowlist, watches the managed process, applies bounded restart backoff, and reaps its process group on shutdown. The child watches its original parent PID. Missing dependencies are not downloaded at startup.

**ARC-003.** One installed swarm service may host multiple logical families. In the delivered implementation, all control objects in one node database share one static Raft committee. Therefore every controller is trusted with the complete contents of that committee's replicated log. This is not permissionless multi-tenant controller isolation. Do not place mutually private administrative realms under different untrusted controllers of the same database.

The target architecture MUST shard committees, logs, encryption and read authority by realm or explicitly co-governed realm group. A global family index may contain public lineage/frontier metadata, but private work graphs, dataset grants and KV directories must stay in their realm. Checkpoint references can cross boundaries only through approved promotion/export.

## Identities, authorities and capabilities

The sidecar generates ML-DSA-44 identities through OpenSSL and signs its own `BTX-SWARM/1` envelopes. These are real cryptographic signatures, but they are not automatically BTX PIT identities or native PQ1 sessions. The native bridge must bind a sidecar key and certificate to a currently authorized PIT principal, delegation, realm, operation set, expiry and revocation epoch. Copying a wallet seed or returning a native private key is forbidden.

A transport pin proves the endpoint associated with a configured key. A role grant authorizes specific protocol operations. A resource policy permits local machine use. A data grant permits specified input and derivative release. An evaluation report supplies bounded evidence. A payment mandate limits financial authority. None of these substitutes for any other.

**ARC-004.** Trust-domain labels come from the realm's admitted policy, not from a participant's self-declared location. The code enforces label diversity but does not independently prove organizational independence. Public deployment requires an explicit trust registry, auditable owner changes and defenses against root multiplication.

## Data stores and crash domains

`control.sqlite3` persists replicated commands, results, events and materialized objects. `discovery.sqlite3` stores expiring cards and local subscriptions. The encrypted blob store uses separate object indexes and per-realm keys. Each worker has a distinct process-private working directory and cache. `resources.sqlite3` arbitrates device-wide resource leases. `adoption.sqlite3` is the authority for local model selection. Payment accounting uses a separate database and local socket.

SQLite transactions are FULL-synchronous WAL transactions; metadata mutations use savepoints so invalid operations produce typed, replicated rejections rather than partial state. Artifacts are committed independently and referenced only after digest verification. A crash may leave orphaned staging bytes, never permission to trust incomplete bytes. Orphan reclamation and long-lived quota accounting require further hardening.

## Service interfaces and authority flow

Local management uses a private Unix socket. Peer control and artifact endpoints use pinned mutual TLS. Control requests have signed envelopes; blob requests use the already authenticated peer plus current realm role checks. Control and artifact routes are logically distinct but currently share one server implementation. The portable transfer path is CPU-buffered HTTPS; it is not an implemented RDMA path.

Workers cannot become voters by publishing presence. They cannot grant themselves resource budgets or access to another realm's cache. Controller election cannot transfer realm ownership. A new leader receives authority to sequence the existing journal, not authority to rewrite policies or spend funds.

## Distributed scale target

Use a sparse global public discovery overlay, bounded neighborhood caches, realm-local committees and hierarchical training cohorts. Do not replicate every experiment, KV page or participant presence to every BTX node. Control shards should reference content-addressed artifacts rather than embedding tensors. Scale claims require measured workload curves, adversarial churn and loss tests; the current single-host tests do not establish global throughput.

A missing engine is advertised as `ENGINE_ABSENT` and moves no bytes. A transport pin does not authorize a role, a resource policy, a data grant, an evaluation, or a payment. A self-declared location does not replace the realm policy label. Scale claims in this tree are single-host unit tests. They are not a measured global throughput.
