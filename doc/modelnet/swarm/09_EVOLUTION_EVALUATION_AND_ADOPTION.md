# BTX Swarm 0.35 — Evolution evaluation and adoption

Document: **BTX-SWARM-SPEC-09**, revision **0.1.0**, 19 October 2026.

**Implementation scope:** Candidate frontiers, paired evidence bounds, signed certificate verification, channel transitions and reversible local activation are implemented. Public evaluator independence and automatic production-runtime adoption remain unqualified.

## Population model

**EVO-001.** A family contains immutable candidates and persistent channels, not one mandatory global model. Candidate types include base, adapter, distillation, merge and architecture changes. Parents must belong to the family and remain available as history. A new head may supersede its parents on the visible frontier without deleting them. Different niches preserve distinct useful descendants.

The code contains a Pareto-frontier helper maximizing declared quality/novelty and minimizing cost within each niche. Research allocation reserves an exploration fraction rather than assigning every slot to the current top score. These helpers are executable, but not yet a complete autonomous network-wide research allocator. Declared fitness must be independently derived from accepted evidence before it controls funded capacity.

## Predeclared evaluation

**EVO-002.** Commit the candidate's objective, suite, holdout/sample policy, allowed trial count, primary metric, regression ceilings, required evaluator domains and canary policy before scoring. Candidate submission consumes a persistent trial budget. Repeating the same candidate cannot reset it; exceeding the budget requires an explicitly new approved evaluation campaign, not arbitrary resubmission.

The implemented metric is paired binary task success. Counts `00`, `01`, `10`, `11` mean parent/candidate failure/success on the same distinct cases; `01` is an improvement, `10` a regression. Let `n` be the number of distinct cases and `d=(n01−n10)/n`. For family error budget `alpha` and maximum trials `K`, the conservative radius is `sqrt(2 log(K/alpha)/n)` for differences bounded in [-1,1]. Lower and upper bounds are scaled and rounded conservatively using high-precision Decimal arithmetic.

**EVO-003.** The code requires enough cases, a minimum observed gain, a strictly positive lower bound, complete regression metrics under their ceilings, exact suite/holdout bindings and a reproducibility assertion. It does not claim that a positive bound proves the absence of backdoors, contamination or catastrophic forgetting.

Evaluator signatures are not extra samples. Reports on the same sample commitment may independently attest the test execution, but their sample counts must not be added together to shrink uncertainty. The current implementation uses each report's qualified bound and requires a shared sample commitment.

## Independent evidence and certification

**EVO-004.** Evaluators must hold an admitted evaluator role, not be the declared candidate author, and span the configured number of policy-assigned trust domains. Reports preserve negative outcomes. A certificate references a unique report set, candidate, family, niche-specific channel and policy epoch. Neither 51% of arbitrary identities nor a raw quorum of signatures establishes quality.

The code checks declared roots and labels. It does not establish real administrative independence, exclude every undisclosed contributor behind a publisher, or prove that raw counts correspond to correct hidden-test execution. Public qualification needs full contributor/provenance closure, independently maintained attestor trust, reproducible evaluation environments, protected challenge pools and audits against evaluator collusion.

Original ML-DSA signatures are retained with evaluation records. Local certificate verification recomputes candidate and certificate IDs, binds reports to the signed bodies, checks current eligible roots and revocations, validates sample agreement and reruns the quality rule. Expired transport authentication does not erase archival research evidence; current revocation eligibility is checked separately.

## Graduation, channel and activation are different

**EVO-005.** Graduation certifies a candidate for a defined niche. Native Lab admission determines whether that artifact may be served under a native assurance policy. A channel proposes deployment. A local runtime chooses whether to activate it. Each is a separate gate; a certificate must not smuggle new code execution, additional data access or a broader financial mandate.

Channel lifecycle: previous generation → CANARY → ACTIVE, or explicit rollback. Stage uses expected sequence and refuses overlapping canaries. Observations are bound to channel generation. Activation rechecks evaluator revocation. Rollback increments the channel sequence and retains both old and replaced checkpoint references.

Local `Adopter` verifies checkpoint bytes, loads the supported reference decoder and performs a bounded runtime smoke before staging. `adoption.sqlite3` is authoritative; exported pointer JSON is explicitly advisory. Activation requires a matching verified certificate result and local canary result. Repeated delivery repairs advisory output without losing the previous generation. Rollback binds the expected active checkpoint and is idempotent.

**EVO-006.** A production automatic follower must fetch and verify the complete certificate/evidence closure, obtain sufficiently fresh policy/revocation state, preserve a rollback checkpoint, enforce runtime/license compatibility, and activate only the locally subscribed channel. Changing the community baseline cannot forcibly replace a user's pinned local model. The automatic production follow loop and native engine-generation binding are not wired in this package.

## Canary quality and conflicts

Canary count is currently a count of unique peer/receipt observations, not proof of distinct independent deployment sites. Public policies must also bind a predetermined workload, eligible canary panel, minimum duration/load and independent failure reporting; otherwise one participant can fabricate many claimed checks. Conflicting signed observations remain evidence and should pause deployment rather than silently choose a favorable subset.

A specialist that improves coding while harming broad reasoning may graduate under `coding/stable`, not automatically `general/stable`. Merges and distillation require their own evaluation campaign; preserving family ancestry does not make parameters algebraically compatible.

## Executed evolution qualification

The integrated test trains a new checkpoint, recomputes paired synthetic evaluations under three separately signed identities, creates a graduation certificate, rejects altered evidence and revoked evaluators, stages the actual trained tensors and activates a local generation after checks. All identities run on one test host. This is an engineering lifecycle test, not evidence that three independent organizations certified a useful coding model or that the general-purpose model improved.
