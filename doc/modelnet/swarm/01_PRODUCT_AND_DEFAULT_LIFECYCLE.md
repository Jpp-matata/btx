# BTX Swarm 0.35 — Product and default lifecycle

Document: **BTX-SWARM-SPEC-01**, revision **0.1.0**, 19 October 2026.

**Implementation scope:** Target product contract; local default subscriptions and approved private swarms are implemented, universal public model-community joining is not.

## Mission and boundaries

A model is a persistent evolutionary lineage, not merely a download. ModelNet distributes immutable assets and family metadata; Lab preserves provenance and admission evidence; Swarm runs work and learning; SEP accounts for economic objectives; PAY must eventually deliver independently enforceable settlement. Monetary consensus remains unchanged. The executable version in this handoff is a private integration implementation, not a qualified replacement for the 0.35 release candidate.

**LIF-001.** A successfully acquired, byte-verified, complete model MUST cause local observer subscription and model-family discovery without an additional feature switch. Discovery MUST NOT require a GPU, a wallet, a paid account, or a training-capable representation. `CatalogWatcher` implements the local acquisition transition by consuming the verified fields in the pinned `catalog.json` format.

**LIF-002.** A known immutable artifact-to-lineage mapping MUST retain its lineage through packaging, quantization and eligible specialization. Without independently verified mapping, create a private provisional topic from `model_id`; mark it `PROVISIONAL_NOT_AUTHORITATIVE`. Do not assert common ancestry merely because labels, tensor dimensions or repository names match. Provisional topics are not automatically published.

**LIF-003.** Observer subscription enables update awareness, public research metadata and eligible discovery. It does not authorize public seeding, execution, private prompt routing, third-party credentials, data contribution, monetary spending, or automatic admission of untrusted software. There is no implicit training-data collection on ordinary inference.

**LIF-004.** A persistent operator contribution policy controls device-wide slots, CPU threads, VRAM reservation and maximum work per job. Once granted, compatible contribution work is scheduled automatically across joined realms; turning contribution off drains managed processes. Budget and consent are authority, not feature flags. Battery, temperature, metered-network and hard host-memory enforcement remain additional required governors before general desktop release.

**LIF-005.** Improvements MAY coexist as general, specialist, experimental and hardware-specific descendants. A family maintains a frontier, not one universally forced checkpoint. A community can continue to evolve while an individual remains an observer or pins an older generation.

## Lifecycle

| Event | Required default | Executed implementation |
|---|---|---|
| Model download incomplete | No usable subscription or runtime | Watcher rejects incomplete artifacts |
| Verified download completes | Establish family observer subscription | Persistent SQLite subscription; provisional mapping stays private |
| Public mapped family appears locally | Advertise bounded family presence | Signed LAN card; public mappings only |
| Operator accepts invitation | Join exactly the signed realm and roles | ML-KEM-sealed invitation, issuer pin, replay rejection |
| Operator grants idle contribution | Schedule permitted work automatically | Joined worker loop and device-wide governor |
| Compatible cached state exists | Compare safe reuse options | Exact reference KV reuse; cost helper; production engine plans unqualified |
| Candidate obtains sufficient evidence | Graduate for its stated niche | Typed reports, quorum/domain policy and multiplicity-adjusted bound |
| Channel passes canaries | Stage and optionally locally adopt | Channel state machine and local adopter library; general automatic follow loop not integrated |
| A regression is detected | Retain/revert the previous eligible generation | Explicit channel/local rollback code; automatic fleet detection remains required |
| Objective is funded | Show backing and payable liquidity separately | Observed-backing ledger; zero enforceable or immediately payable L2 balance |

## User experience and deployment modes

Ship `btx-swarmd`, `btx-swarm`, and `btx-paynetd` with ModelNet. The native patch starts the swarm service automatically when `btx-modeld` starts. Do not add a second CMake enable switch. Ordinary monetary startup and validation must not depend on the swarm service becoming healthy. A missing wheel bundle causes a visible degraded sidecar, not silent monetary failure or an online dependency installer.

Fresh initialization selects an IPv4 route interface and an authenticated listener automatically. Its address is only a reachability hint; NAT and public reachability must be measured separately. Existing configurations are never silently rewritten. Local administration remains a private Unix socket with a token, not an internet-exposed API.

The executable supports a single-node default controller or a preconfigured static controller committee. Joining a work realm does not join that controller committee. Complete automatic family-wide rendezvous, decentralized public membership, dynamic committee membership and native identity binding are outstanding product requirements, not hidden configuration options that already work.

## Explicit non-goals and compatibility

Do not alter BTX block validity, proof of work, issuance, transaction serialization, chain selection or wallet ownership. Do not infer that a qualified research object is qualified software. Do not infer that an open download grants unrestricted rights to train or redistribute derivatives. Do not promise that an open model manifest can tax all independent uses of the weights. Hugging Face and other artifact origins remain compatible import/export destinations subject to independently checked provenance and permissions.

## Product acceptance

A release demonstration must begin with a clean install and an eligible model acquisition, not preseeded test databases. It must display the joined family, remaining permissions, active device budget and followed channel. At least three independently administered machines must maintain the community after one coordinator disappears. The included tests exercise pieces of this journey and an integrated synthetic model-evolution path, but not that complete public multi-organization product demonstration.
