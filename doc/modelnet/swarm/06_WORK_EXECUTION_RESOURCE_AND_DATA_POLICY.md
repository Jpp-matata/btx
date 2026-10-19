# BTX Swarm 0.35 — Work execution resource and data policy

Document: **BTX-SWARM-SPEC-06**, revision **0.1.0**, 19 October 2026.

**Implementation scope:** Bounded trusted subprocesses, device-wide slot/VRAM accounting, environment isolation and release rechecks are implemented. General sandboxing and hard host-memory/power controls remain blockers.

## Executable operations

The current trusted executor set is `torch.train`, `torch.rollout`, `torch.evaluate`, `dataset.verify` and the reference `inference` operation. `sandbox` is not accepted as an implemented executor. Uploaded Python, pickle, dynamic model code and arbitrary shell commands are not execution inputs.

**EXE-001.** A model artifact never grants permission to install or execute software. Workers invoke a local installed interpreter and trusted module with explicit arguments; model-provided environment, command names and import paths are not honored. Engine processes use allowlisted inherited environment variables. Cloud credentials, model-hub tokens, wallet variables, RPC cookies, private keys, loader injection and Python path injection are not inherited.

**EXE-002.** Local contribution requires a persistent finite policy. A realm may reduce the operator's ceiling but may not increase it. A device-wide SQLite governor prevents independently joined realm workers from each assuming that the entire GPU is free. Concurrent tasks acquire reservations atomically; compatible ready tasks may run in parallel up to the shared budget.

The code controls advertised slots, VRAM reservations, CPU thread settings and maximum optimizer steps. It does not yet measure actual idle state, battery condition, power, thermal state, metered bandwidth or enforce a cgroup memory ceiling. The setting named `idle_contribution` is authorization to run background work, not a claim of an implemented idle detector. Those missing governors are mandatory for unattended general-purpose desktop deployment.

## Input and output handling

Checkpoint and dataset bytes are acquired by immutable references, checked through the artifact store, and exported into a private task/lease directory. SafeTensors replaces pickle for tensor state. Model inputs cannot choose arbitrary absolute output filenames. Child result paths must remain within the managed task directory.

**EXE-003.** Secret-bearing control fields are rejected. Raw token arrays in replicated jobs require an explicit `PUBLIC_SYNTHETIC` declaration; private prompts are not routed through the control log. That classification is an authorized submitter assertion, not a content-based proof that no secret exists. There is no generic classifier that can safely infer permission from prompt contents.

For private data, use a local capability bound to realm, immutable data artifact, purpose, expiry and revocation state. `TRAIN` and `RELEASE_DERIVATIVE` are distinct permissions. Cache reuse and cache publication are likewise distinct. Model gradients and trained weights may themselves leak sensitive data; keeping raw files local is not a universal privacy guarantee.

**EXE-004.** Re-read local data authorization before execution, after child completion, before uploading derivative bytes/checkpoint state, and before completion publication. Expired or revoked grants fail the release gate. A revoked worker must also fail current peer membership and lease renewal at the coordinator. Already delivered plaintext cannot be recalled; transport and grant revocation are forward-looking.

The implementation rechecks each release stage. Chunk-by-chunk private derivative export and a fully independent credential-owner release service are not yet integrated. Until those boundaries are qualified, do not advertise a general private-data training exchange.

## Result semantics

A worker result includes a digest, executor/version-scoped evidence and logical completion state. It is not a proof of correct computation, a guarantee of improved model quality, or an irrevocable financial receipt. A trusted evaluator may accept it under a separate work contract. Negative experiments remain publishable and may earn contractually agreed base compensation rather than only winner bonuses.

## Process lifecycle

Managed children run in their own process groups. The worker renews leases while waiting; stopping contribution, lease rejection or shutdown triggers termination, a bounded wait, escalation if necessary, and a final wait for exit. Reservations are released only after this physical boundary. Controller and local worker failure can leave conservative held resources, which recovery must reconcile rather than hide.

The portable runner suppresses child stdout/stderr rather than logging arbitrary prompts; structured error details are bounded. Production observability must add privacy-preserving diagnostics and sanitized failure traces without introducing prompt/credential logging through monitoring agents.

## Required sandbox profile

A general coding or sysadmin executor must specify network egress, filesystem mounts, process limits, temporary disk, syscall restrictions, source snapshot, dependency lock, permitted tools and external effects. It must run under an appropriate sandbox/container/VM boundary, not merely a Python subprocess. Remote manifests must never select host interpreters or provider credentials. This executor is not implemented in this handoff.

## Acceptance

Tests cover provider-secret exclusion, permission-revoked derivative release, expired/current work leases, physical cancellation accounting, fresh observer onboarding, actual finite training and inference subprocesses, and shared-cache reuse. Mandatory remaining tests include hard RAM/disk exhaustion, thermal and battery transitions, task starvation, external-tool authorization, multiple real GPUs competing across realms and arbitrary malicious model-format inputs under production runtimes.
