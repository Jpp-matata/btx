# Operator surface

The swarm an operator is running is a thread inside `btxd`. ModelNet defaults on. `-modelnet=0` turns the thread off. There is no swarm startup flag and no second binary.

`src/init.cpp` calls `modelnet::StartSwarmObserver` after the model helper, passing the node model directory. `StopSwarmObserver` runs at shutdown and joins the thread. A failure inside the thread is logged. The monetary node continues. `btxd` does not launch `btx-swarmd` or `btx-paynetd`. The reference Python package is not the running control plane.

## Current behavior

About every two seconds the thread writes the catalog to `observers.json`, refreshes the local mesh, integrates the catalog, calls `SwarmFormPlane`, and calls `SwarmPublishLive`. `observers.json` is observers only: no grant, no seed, and no spend. `FormationPlane` does not exec a sidecar, open a socket, add a DNS seed, broadcast, or spend.

The same wake calls `NifObserve::Tick`, then `SwarmBound::Pass` once. That pass uses the fixed prompt `abcdefgh`, an 8-byte memory echo, and vLLM `ENGINE_ABSENT`. It takes one CPU AdamW step on a 4 by 8 dense net, one one-host two-rank loopback exchange, eight integer replay pairs, one snapshot, and one realm seat. A second host is refused.

`TickBind::Pass` runs once. It runs the requirement binders, a 12-step node cycle with generation 0 and `passed()` false, the local module stores, and the safe slices. It does not quarantine a live model and does not start a guard process.

`Spend` returns `"0"`. `Release` returns `UNQUALIFIED`. `passed()`, `gpu_passed()`, and `windows_passed()` stay false. The latched pass is not the finished network. One machine and two local ranks are not two hosts.

## Engine probe

`SwarmProbeEngine` and `SwarmQualifyEngine` are in-process library calls. The observer thread does not use them to qualify a release, and it does not start vLLM. On the observer path, vLLM stays `ENGINE_ABSENT`.

`SwarmProbeEngine` checks a runtime name and a filesystem path.

- An empty name or path is `ENGINE`.
- A missing path, or a path that is not a regular file, is `ENGINE_ABSENT`. `present`, `advertised`, and `qualified` stay false. The call does not start a process.
- A regular file is `INSTALLED`. It is not advertised.

`SwarmQualifyEngine` may set `advertised` and `qualified` only when the probe already returned `INSTALLED`, the caller sets `measurement_supplied`, and `tokens_per_sec` is finite and greater than zero. That caller-supplied rate is not a hardware benchmark. The function does not start an execution engine or a GPU. A missing engine stays `ENGINE_ABSENT`.

No latency, tokens per second, VRAM, or bandwidth figure belongs on this page. None was measured here.

## Direct invoice book

`SwarmInvoiceBook` is an in-process ledger for one direct invoice. The observer thread does not open it, and a row in the book is not a mandate.

- `Open` once. A second open is `REOPEN`.
- `Create` stores a beneficiary, an objective, a positive atom count, and a hashlock. It does not spend.
- `Reserve` binds one liquidity id. A second owner is `DOUBLE_RESERVE`.
- `Reveal` settles one matching 32-byte preimage inside the book. A SHA256 mismatch is `HASHLOCK`.

`MultiHopQualified` stays false. A hashlock is not a qualified routed network. The book has no broadcast flag. `Spend` on the observer path returns `"0"`. Settlement of accepted work stays in [Later releases](#later-releases).

## What this page does not claim

Windows, macOS, a second GPU, a second physical host, and a linked remote execution engine are not operational here. An advertised pool balance is not spendable. The latched pass does not qualify the node.

## Later releases

Follow-on work toward a fully autonomous network:

- Network peer discovery and admission of a local group.
- Acquiring a verified model from another participant.
- Submitting a task or prompt and receiving model work.
- Authorized training checked by a second machine.
- A private defensive case under an explicit customer grant.
- Settlement of accepted work in BTX only under an operator-signed mandate.

Those claims wait on verification with two physical hosts, a second GPU, Windows, macOS, a linked remote execution engine, an offline release image, the hardware profiles, and the sealed acceptance catalogue. One machine and two local ranks are not two hosts. Spend stays 0 until a mandate exists.
