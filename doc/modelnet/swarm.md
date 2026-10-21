# Swarm

The swarm is a thread inside `btxd`. `src/init.cpp` calls `modelnet::StartSwarmObserver` after the model helper when `-modelnet` defaults on. `-modelnet=0` turns the thread off. A failure is logged and the monetary node continues. `btxd` does not launch `btx-swarmd` or `btx-paynetd`. The reference Python package under `contrib/swarm` is not the running control plane.

## Current behavior

About every two seconds the thread writes the catalog to `observers.json`, refreshes the local mesh, integrates the catalog, calls `SwarmFormPlane`, and calls `SwarmPublishLive`. `observers.json` holds observers only: no grant, no seed, and no spend. `FormationPlane` does not exec a sidecar, open a socket, add a DNS seed, broadcast, or spend.

The same wake calls `NifObserve::Tick`, then `SwarmBound::Pass` once. That pass uses the fixed prompt `abcdefgh`, an 8-byte memory echo, and vLLM `ENGINE_ABSENT`. It takes one CPU AdamW step on a 4 by 8 dense net, one one-host two-rank loopback exchange, eight integer replay pairs, one snapshot, and one realm seat. A second host is refused.

`TickBind::Pass` runs once: the requirement binders, a 12-step node cycle with generation 0 and `passed()` false, the local module stores, and the safe slices. It does not quarantine a live model and does not start a guard process.

`Spend` returns `"0"`. `Release` returns `UNQUALIFIED`. `passed()`, `gpu_passed()`, and `windows_passed()` stay false.

This latched pass is a local check in one process. It is not the finished network. One machine and two local ranks are not two hosts.

Piece selection, endgame, peer exchange, and partial piece serving are design notes in [libtorrent-parts-bin-audit.md](libtorrent-parts-bin-audit.md), [piece-selection.md](piece-selection.md), [provider-exchange.md](provider-exchange.md), and [nat-traversal.md](nat-traversal.md). libtorrent and qBittorrent were studied as parts bins and were not vendored. qBittorrent is GPL, so the study is interface only and the code was never copied. The observer thread does not carry out that sharing design.

## Later releases

Follow-on work toward a fully autonomous network:

- Network peer discovery and admission of a local group.
- Acquiring a verified model from another participant.
- Submitting a task or prompt and receiving model work.
- Authorized training checked by a second machine.
- A private defensive case under an explicit customer grant.
- Settlement of accepted work in BTX only under an operator-signed mandate.

Those claims wait on verification with two physical hosts, a second GPU, Windows, macOS, a linked remote execution engine, an offline release image, the hardware profiles, and the sealed acceptance catalogue. One machine and two local ranks are not two hosts. Spend stays 0 until a mandate exists.
