# Operator surface

Modelnet starts inside `btxd`. The default is on. Joining the local catalog does not require a second binary and does not require an extra swarm startup flag. `StartSwarmObserver` runs when modelnet is enabled and receives the node model directory. `StopSwarmObserver` runs at shutdown. The swarm control plane is this process. `btxd` does not launch `btx-swarmd` or `btx-paynetd`.

## Engine probe

`SwarmProbeEngine` checks a runtime name and a filesystem path.

- An empty name or path is `ENGINE`.
- A missing path or a path that is not a regular file is `ENGINE_ABSENT`. `present`, `advertised`, and `qualified` stay false. The call does not start a process.
- A regular file is `INSTALLED`. It is not advertised.

`SwarmQualifyEngine` may set `advertised` and `qualified` only when the probe already returned `INSTALLED`, the caller sets `measurement_supplied`, and `tokens_per_sec` is finite and greater than zero. That caller-supplied rate is not a hardware benchmark. The function does not start vLLM, SGLang, LMCache, torch, or a GPU. A missing engine stays `ENGINE_ABSENT`.

No latency, tokens per second, VRAM, or bandwidth figure belongs in this document. None was measured here.

## Direct invoice book

`SwarmInvoiceBook` accounts one direct invoice:

- `Open` once. A second open is `REOPEN`.
- `Create` stores a beneficiary, an objective, a positive atom count, and a hashlock. It does not spend.
- `Reserve` binds one liquidity id. A second owner is `DOUBLE_RESERVE`.
- `Reveal` settles one matching 32-byte preimage. A SHA256 mismatch is `HASHLOCK`.

`MultiHopQualified` stays false. A hashlock is not a qualified routed network. The book has no broadcast flag. `automatic_spend_atoms` stays `"0"`. The scheduler has no spend key.

## What this page does not claim

Windows was not built or tested by this page. A GPU, a two-machine run, and a production inference engine are not described as operational. An advertised pool balance is not spendable.
