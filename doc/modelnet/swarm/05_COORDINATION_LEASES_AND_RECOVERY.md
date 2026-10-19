# BTX Swarm 0.35 — Coordination leases and recovery

Document: **BTX-SWARM-SPEC-05**, revision **0.1.0**, 19 October 2026.

**Implementation scope:** Persistent static-committee Raft and work fencing are executable and tested. Dynamic membership, Byzantine committees and distributed external-effect transactions are not implemented.

## Coordination model

**COO-001.** A pinned static committee runs crash-fault Raft. Leaders sequence only authorized realm operations. A controller has no authority over unrelated BTX nodes or monetary consensus. Workers are non-voters. The implementation permits 1–31 voter identities; the quorum is a strict majority of the configured set.

The default fresh node has one voter and therefore no controller redundancy. The failover tests use three explicitly configured voters. There is no claim that a default single-node community survives loss of its only controller. Growing a committee by editing live configuration is rejected by a persisted committee hash. Production enrollment requires joint-consensus membership transitions and catch-up, not disabling this guard.

**COO-002.** Persist term and vote before responding. Compare last-log term/index when voting. Reject mismatched predecessors and never truncate a committed prefix. A leader only advances commit using an entry in its current term, after required replica acknowledgments. Followers apply the same deterministic sequence. Commands that fail schema or policy still produce durable, idempotent rejections.

Snapshots include materialized control tables, command outcomes and authorization evidence. They bind index and term and are bounded. A node cannot manufacture a later applied state by loading unrelated application rows. The snapshot path was tested in protocol-level tests, while large production snapshot streaming and encrypted multi-realm snapshots remain qualification work.

## Work graph

**COO-003.** A batch contains at most 128 tasks. It commits atomically after cycle, duplicate, dependency-realm, deadline, allowed executor and payload checks. New dependencies may reference tasks in the same batch or existing tasks in the same realm. A dependency must be COMPLETE before a child is claimable.

Task states are QUEUED, RUNNING, CANCELLING, CANCELLED, COMPLETE and FAILED. Attempts are bounded by realm policy. Task completion carries immutable result and evidence references; it is not itself payment acceptance or proof that the computation was useful.

**COO-004.** A claim is bound to the worker, task, controller term, attempt and process generation, with a finite expiry no later than task deadline. A current signed presence must advertise an eligible executor and enough remaining slots/VRAM. Atomic committed claims prevent two current valid owners of one attempt.

Renewals and completion must use the exact lease and term. A new leadership term requeues old RUNNING tasks for a new attempt and fences old results. It does not pretend the old hardware stopped. Duplicate computation may occur during recovery, but stale completion cannot become authoritative work.

## Logical cancellation versus physical resources

**COO-005.** Logical cancellation sets CANCELLING for a running task. Do not free its memory reservation immediately. Release follows physical subprocess exit or a signed worker quiescence acknowledgment. Workers remain able to acknowledge quiescence after their membership is revoked. A signed restart generation retires old allocations as a worker assertion, not as a hardware-attested fact.

If the coordinator cannot observe quiescence, resource accounting remains conservative. Node-local crash recovery checks Linux process birth stamps to avoid mistaking a reused PID for the prior child. Unknown process state remains charged. Physical GPU-transfer leases need stronger runtime-specific completion hooks; a network timeout does not prove DMA cancellation.

**COO-006.** An expired logical lease can be requeued while its old allocation remains charged. This prevents a second task from consuming memory that the first child may still hold. Internal invariant scans refuse truncated views. Terminal allocation/event retention needs compaction before indefinite production operation.

## Failure matrix

| Failure | Required response |
|---|---|
| Leader disappears before majority commit | No committed success; retry original semantic ID |
| Reply disappears after commit | Return stored result on retry; do not duplicate effect |
| Majority partition unavailable | Stop new authoritative mutations; keep local observations explicitly stale |
| Old leader returns | Step down, reconcile logs and reject stale leases |
| Worker crashes | Preserve reservation until verified local recovery or accepted restart assertion |
| Worker revoked during execution | Reject renew/finish/new artifact access; drain the process |
| Transfer times out | Keep destination buffers reserved until backend proves quiescence |
| Artifact source disappears | Retrieve another verified replica or recompute compatible state |
| Whole committee lost | Recover an audited consistent backup or create an explicitly forked realm; never claim same history from an arbitrary survivor |

## Exactness and security limits

Raft protects against crashes and partitions, not malicious majorities. TLS and ML-DSA prevent unauthorized impersonation but do not make a malicious admitted controller honest. High-assurance public realms need Byzantine-aware certification, independent data availability and a tested controller admission policy.

External APIs, purchases and irreversible system administration are not executed by the current reducer. Exactly-once tools require action-specific authorization, outcome queries, idempotency and compensation protocols. Do not turn the reference `work.finish` endpoint into blanket authority for arbitrary remote shell actions.

Default election/heartbeat timings are qualification defaults for the test topology. WAN deployments need measured jitter, backpressure, bounded signing/verification pools and fault injection; simply raising the worker count is not evidence of scalable coordination.
