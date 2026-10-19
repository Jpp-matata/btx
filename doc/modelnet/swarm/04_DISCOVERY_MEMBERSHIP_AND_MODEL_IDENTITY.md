# BTX Swarm 0.35 — Discovery membership and model identity

Document: **BTX-SWARM-SPEC-04**, revision **0.1.0**, 19 October 2026.

**Implementation scope:** Signed LAN discovery, sealed invitations, persistent observer subscriptions and neighborhood ranking are implemented; public WAN rendezvous and autonomous global joining remain integration requirements.

## Three overlapping networks

**DIS-001.** Maintain a persistent model-family network for lineage, public research and channels; a low-latency execution neighborhood for compatible runtimes; and training/research cohorts for exact checkpoints and recipes. Membership in one is not evidence of membership, trust or tensor compatibility in another.

The lineage is broader than a checkpoint. Full-precision weights, quantized artifacts and adapters can refer to one family while having different training and KV compatibility. A signed conversion or lineage claim must identify exact source and destination artifacts. A filename, model hub tag or an equal tensor shape is not provenance.

## Local discovery implementation

The implementation uses IPv4 multicast `239.255.42.35:29449`, TTL 1. Signed cards contain peer identity, endpoint, bounded public lineage IDs and supported protocol names. Cards expire, are capped at 12,000 bytes, and are verified under per-source rate limits and a bounded verification queue. At most 1,024 discovery entries are kept; advertisements contain at most sixteen lineage IDs. Unknown cards remain metadata-only.

These are LAN packets, not an mDNS implementation and not a WAN overlay. ML-DSA cards may exceed a single network MTU; fragmentation resilience has not been qualified. Production discovery should use compact rendezvous beacons and fetch signed metadata through a bounded authenticated transport, or integrate the existing ModelNet discovery plane. Do not increase UDP sizes and call that WAN support.

Fresh `host=auto` chooses a route interface without transmitting a discovery probe to the route-selection address. It binds authenticated service traffic on IPv4 wildcard and advertises the selected interface. Fallback is loopback. Neither action proves public reachability or authorizes NAT mappings. Explicit host configuration is preserved across restarts.

## Invitation and admission sequence

**DIS-002.** A recipient creates a self-signed peer bundle containing ML-DSA identity, ML-KEM-768 public key, TLS certificate and HTTPS endpoint. Bundle verification binds the certificate key to signing identity. The inviter pins that bundle through local administrative input; a discovered public card is insufficient.

**DIS-003.** The inviter separately commits `member.grant` with exact realm, roles, admitted trust domain and capacity ceilings. The invitation is then encrypted for the specific recipient using ML-KEM-768, HKDF-SHA384 and AES-GCM, and signed by the inviter. Header includes issuer, recipient, nonce and expiry. The recipient supplies the expected issuer ID out of band and rejects wrong recipients, expired invitations, modified ciphertext and previously consumed nonces.

**DIS-004.** The current CLI shares a realm data key only with worker/trainer/evaluator invitations, not observers. That key enables previously retained ciphertext to be read and cannot revoke plaintext already received. Revocation blocks future server access and work, but public release requires membership-epoch key rotation and private cache invalidation.

Joining writes the remote realm's committee/peer profile to `joined.json`. It does not overwrite the joining node's own controller committee. Granting work membership does not turn on idle contribution: local device consent is a separate persistent decision.

## Neighborhood selection

Rank eligible peers by measured round-trip latency, usable throughput, error rate, runtime/checkpoint residency, queue delay, trust and data permissions. The reference selector favors four nearby authorized peers plus two additional independent-domain peers; those numbers are heuristic defaults, not an optimality result. “Nearby” means network performance, not geographic distance. Always retain routes for global information exchange when permitted.

A useful inference neighbor may not be a useful evaluator or payment router. Each overlay chooses its own peers. The scheduler must not prefer a nominally huge funded objective over available executable work with reliable payment terms.

## Public family bootstrap contract

A public lineage manifest needs an immutable genesis, creator/provenance binding, licensed origin, discovery policy and accepted governance roots. A native ModelNet publication must bind the manifest to verified downloaded artifacts. Unclaimed imported models may use provisional local topics but cannot silently inherit the authority of the original publisher.

Automatic public membership must be sparse, expiring and revocable. Joining must not download arbitrary models or run publisher code. Trust should be established through direct pins, independently admitted roots and signed realm policies, with limits on Sybil influence. NATed peers must be able to operate through outbound connections and bounded relays without publishing wallet/RPC endpoints.

The delivered code does not implement this full public bootstrap/rendezvous/relay path. A coordinator must integrate it with native ModelNet and test independent networks, packet loss, churn, NAT traversal, private-model invisibility and stale registry records before advertising universal download-to-swarm joining.
