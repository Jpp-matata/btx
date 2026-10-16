# Balance watch

Balance watch reports whether this wallet's balances are proven through the chain tip. Until the watch is ready, an amount asked from it is not known. Unknown is not the same as zero.

The Qt overview page shows one line under the balance: `Balances proven through block N` when the watch is ready, or `Balances not ready, N blocks behind` when it is not. That line does not replace the balance amount.

## getbalancewatch

`getbalancewatch` returns the status of this wallet's watch. It does not return an amount. A false `ready` does not imply a balance.

`ready` is true only when the chain has a tip height, the wallet has processed that height, and the wallet is not scanning. The status also includes `chain_height`, `wallet_height`, `blocks_behind`, `scanning`, `descriptors`, and `fetched`. `blocks_behind` is how many blocks the wallet is behind the tip when both heights are known. When the chain is unavailable, `ready` is false and `blocks_behind` is 0.

`descriptors` are this wallet's own public descriptor strings. `fetched` lists descriptor lookups that were not already in that set, at most 32. A descriptor this wallet already has is not listed. `shielded_foreign_watch` is always false.

## getbalanceatheight

`getbalanceatheight height` uses only transactions already stored for this wallet. It does not walk the chain.

When the wallet is ready through that height, the result is:

```json
{ "height": <height>, "amount": <amount>, "ready": true }
```

The amount sums transparent outputs that belong to this wallet, are confirmed at or below `height`, are not immature coinbase at that height, and are not spent by another wallet transaction confirmed at or below `height`.

When `height` is negative, the wallet has not processed `height`, or a scan is running, the command fails and does not include an amount.

## getbalances

`getbalances` keeps the existing trusted, pending, immature, and watch-only amounts on the same formulas. It adds two fields:

- `watch_ready` (bool): whether the watch has reached the chain tip and the wallet is not scanning.
- `blocks_behind` (number): how many blocks the wallet is behind the chain tip. This is 0 when the chain is unavailable.

## BTX_BALANCE_NOT_READY

`BTX_BALANCE_NOT_READY` means the amount is not known yet. `getbalanceatheight` returns this error when it is not ready through the requested height, and the response does not include an amount. Do not substitute zero.

## Scope

Shielded notes this wallet cannot open are omitted. Another person's shielded balance is not watched. Balance watch does not sign and does not spend.
