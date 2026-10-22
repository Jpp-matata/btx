# Swarm integration in this tree

The swarm is integrated as a thread inside `btxd`.

`src/init.cpp` calls `modelnet::StartSwarmObserver` after the model helper when `-modelnet` defaults on. `-modelnet=0` skips that call and logs that the model network is disabled. The monetary node still starts. The observer runs on a `std::thread` owned by the `btxd` process. Shutdown calls `StopSwarmObserver` and joins that thread. If a step throws or returns an error, the thread logs the error with the monetary node continuing. A swarm failure does not stop consensus.

## Current behavior

About every two seconds, while the thread is running:

1. Verified, explicitly complete catalog entries are reconciled into `modeldir/swarm/observers.json`. Removed, unverified or incomplete entries lose their active subscription; an empty or missing catalog clears existing subscriptions. The file is observers only. It records no grant, no seed, and no spend. `automatic_spend_atoms` in that file is the string `"0"`, and payment authority is false.
2. The local mesh is refreshed.
3. The catalog is integrated into `swarm/contribution.json` using the same bounded reader and lineage resolver as the observer.
4. `SwarmFormPlane` runs. `FormationPlane` does not exec a sidecar, open a socket, add a DNS seed, broadcast, or spend.
5. `SwarmPublishLive` runs. A missing contribution file succeeds and spends nothing.

Both views use `swarm/lineage-mappings.json` when an artifact has a local mapping; otherwise they derive a private `PROVISIONAL_NOT_AUTHORITATIVE` topic from `model_id`. Contribution economy IDs derive from that resolved lineage. Catalog `visibility: public` alone cannot publish a provisional topic or set its contribution `seed` flag. Removing a mapping restores the private provisional identity. The existing `VERIFIED_MAPPING` label records trust in local operator policy: this reader does not cryptographically authenticate an external mapping file.

`observers.json` is the last successful catalog snapshot, not a durable follow-history list. Malformed catalog or mapping input fails without replacing that snapshot, and the observer skips live publication for that poll. A failed contribution integration also skips formation and live publication. Retained files alone do not prove current eligibility or authorize work. Successful polls refresh the views; separate file replacements are not an atomic transaction across all swarm files.

The same wake then calls `NifObserve::Tick`, then `SwarmBound::Pass` once, then `TickBind::Pass` once. Later wakes see the bound and the tick bind already latched. They do not repeat the pass.

`SwarmBound::Pass` uses the fixed prompt `abcdefgh`, an 8-byte memory echo, and vLLM `ENGINE_ABSENT`. It takes one CPU AdamW step on a 4 by 8 dense net, exchanges two ranks over loopback on one host, checks eight integer replay pairs, publishes one snapshot, and seats one realm role. A second host is refused. The decode path does not launch a process and does not use a GPU.

`TickBind::Pass` runs the requirement binders, a 12-step node cycle with generation 0 and `passed()` false, the local module stores, and the safe slices. It does not quarantine a live model and does not start a guard process.

`Spend` returns `"0"`. `Release` returns `UNQUALIFIED`. `passed()`, `gpu_passed()`, and `windows_passed()` stay false.

`btxd` does not launch `btx-swarmd` or `btx-paynetd`. `contrib/swarm` remains the reference Python package. `btxd` does not start it. That package is not the running control plane.

Libraries elsewhere in `bitcoin_modelnet` can be called by tests. They are not this control plane. The latched pass is not a peer network, a qualified release, or a settlement. One machine and two local ranks are not two hosts. This host can compile and run the Linux unit suites. A Windows, macOS, or GPU result is recorded only when that run happens on that machine.

## Later releases

Follow-on work toward a fully autonomous network:

- Network peer discovery and admission of a local group.
- Acquiring a verified model from another participant.
- Submitting a task or prompt and receiving model work.
- Authorized training checked by a second machine.
- A private defensive case under an explicit customer grant.
- Settlement of accepted work in BTX only under an operator-signed mandate.

Those claims wait on verification with two physical hosts, a second GPU, Windows, macOS, a linked remote execution engine, an offline release image, the hardware profiles, and the sealed acceptance catalogue. One machine and two local ranks are not two hosts. Spend stays 0 until a mandate exists.
