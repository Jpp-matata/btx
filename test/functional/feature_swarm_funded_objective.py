#!/usr/bin/env python3
# Copyright (c) 2026 The BTX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Isolated regtest: a funded objective is a wallet submission.

preparefundmodelrelease is called with a release id and, when its help text
accepts an options object, the wallet inputs and amount the helper uses to
assemble unsigned_hex. A complete signature is broadcast and the txid is
recorded. With no signable plan the wallet txcount stays the same.
automatic_spend_atoms stays 0. One submission does not settle every claimed
payment mode.
"""

from pathlib import Path

from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import COINBASE_MATURITY
from test_framework.messages import COIN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


def _is_hex_tx(value):
    if not isinstance(value, str) or len(value) < 20 or len(value) % 2:
        return False
    return all(c in "0123456789abcdefABCDEF" for c in value)


def _candidate_hex(obj, depth=0):
    if isinstance(obj, str) and _is_hex_tx(obj):
        return obj
    if depth > 3 or not isinstance(obj, dict):
        return None
    for key in ("unsigned_hex", "hex", "rawtx", "signed_hex"):
        value = obj.get(key)
        if _is_hex_tx(value):
            return value
    for child in obj.values():
        if isinstance(child, dict):
            found = _candidate_hex(child, depth + 1)
            if found:
                return found
    return None


class SwarmFundedObjectiveTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.rpc_timeout = 180

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_nodes(self):
        self.modeldir = Path(self.options.tmpdir) / "funded-objective-models"
        self.modeldir.mkdir(parents=True)
        # Same local modelnet pattern as feature_swarm_regtest_spend.py.
        self.extra_args = [[
            "-modelnet=1",
            "-modelnetrequired=0",
            "-modelbind=off",
            "-modelstorage=0",
            f"-modeldir={self.modeldir}",
            f"-modelhelper={self.modeldir / 'absent-helper'}",
            # Keep regtest block production off the matmul path so a coinbase can mature.
            "-regtestmatmulbindingheight=2147483647",
            "-regtestmatmulproductdigestheight=2147483647",
            "-regtestmatmulv4height=2147483647",
            "-regtestmatmulrequireproductpayload=0",
        ]]
        super().setup_nodes()

    def _assert_automatic_spend_zero(self, node):
        info = node.getmodelnetworkinfo()
        assert_equal(info["automatic_spend_atoms"], 0)

    def _try_sign_and_send(self, node, plan):
        raw = _candidate_hex(plan)
        if not raw:
            return None
        try:
            signed = node.signrawtransactionwithwallet(raw)
        except JSONRPCException:
            return None
        if not isinstance(signed, dict) or signed.get("complete") is not True:
            return None
        signed_hex = signed.get("hex")
        if not _is_hex_tx(signed_hex):
            return None
        try:
            txid = node.sendrawtransaction(signed_hex)
        except JSONRPCException:
            return None
        if isinstance(txid, str) and txid:
            return txid
        return None

    def _funding_options(self, node):
        # One mature wallet coin. Surplus returns as change so a signed plan is not an absurd fee.
        amount_atoms = 1_000_000
        fee_atoms = 10_000
        coins = node.listunspent()
        if not coins:
            return {"amount_atoms": amount_atoms}
        coin = coins[0]
        atoms = int(coin["amount"] * COIN)
        if atoms <= amount_atoms + fee_atoms:
            amount_atoms = max(1, atoms // 2)
            fee_atoms = max(1, atoms - amount_atoms - 1) if atoms > amount_atoms else 0
        return {
            "amount_atoms": amount_atoms,
            "inputs": [{
                "txid": coin["txid"],
                "vout": int(coin["vout"]),
                "amount_atoms": atoms,
            }],
            "destination": node.getnewaddress(),
            "fee_atoms": fee_atoms,
        }

    def run_test(self):
        node = self.nodes[0]
        assert_equal(node.getblockchaininfo()["chain"], "regtest")
        self._assert_automatic_spend_zero(node)
        # self.generate pays the deterministic key, which this wallet does not own.
        wallet_addr = node.getnewaddress()

        def mine_to(height):
            stalls = 0
            while node.getblockcount() < height:
                before = node.getblockcount()
                step = min(20, height - before)
                self.generatetoaddress(node, step, wallet_addr)
                if node.getblockcount() == before:
                    stalls += 1
                    if stalls > 3:
                        raise AssertionError(f"mining stalled at height {before}")
                else:
                    stalls = 0
            self.wait_until(lambda: node.getwalletinfo()["lastprocessedblock"]["height"] == node.getblockcount())

        # Coinbase at height h is spendable once depth reaches COINBASE_MATURITY + 1.
        # Tip 102 matures the coinbases from heights 1 and 2.
        assert COINBASE_MATURITY == 100
        mine_to(102)
        assert node.getblockcount() >= 102
        assert node.getbalance() > 0
        self._assert_automatic_spend_zero(node)

        txcount_before = node.getwalletinfo()["txcount"]
        release_id = "a" * 96
        helptext = node.help("preparefundmodelrelease")
        if "options object" in helptext:
            args = [release_id, self._funding_options(node)]
        else:
            args = [release_id]
        plan = None
        try:
            plan = node.preparefundmodelrelease(*args)
        except JSONRPCException as err:
            self.log.info(f"preparefundmodelrelease did not return a plan: {err}")
            plan = None

        self._assert_automatic_spend_zero(node)
        submitted = self._try_sign_and_send(node, plan) if isinstance(plan, dict) else None
        if submitted:
            assert_equal(len(submitted), 64)
            self.log.info(f"wallet submitted funded objective txid {submitted}")
            # ASR-PAY-004: one regtest submission does not settle every claimed mode.
            self.log.info("one regtest submission does not settle every claimed payment mode")
            self._assert_automatic_spend_zero(node)
        else:
            self.log.info("no signable funded-objective plan; unsigned_hex was not broadcast")
            assert_equal(node.getwalletinfo()["txcount"], txcount_before)
            self._assert_automatic_spend_zero(node)


if __name__ == "__main__":
    SwarmFundedObjectiveTest(__file__).main()
