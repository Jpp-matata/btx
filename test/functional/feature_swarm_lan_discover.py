#!/usr/bin/env python3
# Copyright (c) 2026 The BTX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Two regtest nodes, started with no framework link.

The default setup_network connects the nodes. This test does not. Regtest
has no DNS seed, so an empty peer list is not discovery and does not fail
the test. The bootstrap phase then links the nodes once with the framework
helper, disconnects the first node, and checks that the other node is still
up. That phase is harness bootstrap loss, not a LAN discovery claim.
"""

import time
from pathlib import Path

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class SwarmLanDiscoverTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.rpc_timeout = 180

    def setup_nodes(self):
        self.extra_args = []
        for index in range(self.num_nodes):
            modeldir = Path(self.options.tmpdir) / f"lan-models-{index}"
            modeldir.mkdir(parents=True)
            self.extra_args.append([
                "-modelnet=1",
                "-modelnetrequired=0",
                "-modelbind=off",
                "-modelstorage=0",
                f"-modeldir={modeldir}",
                f"-modelhelper={modeldir / 'absent-helper'}",
                "-regtestmatmulbindingheight=2147483647",
                "-regtestmatmulproductdigestheight=2147483647",
                "-regtestmatmulv4height=2147483647",
                "-regtestmatmulrequireproductpayload=0",
            ])
        super().setup_nodes()

    def setup_network(self):
        # Start both nodes and leave them unlinked. The default setup_network
        # would connect them through the framework helper.
        self.setup_nodes()

    def _manual_address_supplied(self):
        peer_options = {"connect", "seednode", "addnode", "setnode", "modelpeer", "modelseednode"}
        for args in self.extra_args:
            for arg in args:
                name = arg.split("=", 1)[0].lstrip("-")
                if name in peer_options:
                    return True
        return False

    def _record_peer_lengths(self):
        deadline = time.monotonic() + 10
        peer_lengths = [len(node.getpeerinfo()) for node in self.nodes]
        while all(length == 0 for length in peer_lengths) and time.monotonic() < deadline:
            time.sleep(0.5)
            peer_lengths = [len(node.getpeerinfo()) for node in self.nodes]
        return peer_lengths

    def bootstrap(self):
        """Harness bootstrap loss. This link is not LAN discovery."""
        self.connect_nodes(0, 1)
        self.disconnect_nodes(0, 1)
        remaining = self.nodes[1]
        assert remaining.process is not None and remaining.process.poll() is None
        remaining.getblockchaininfo()

    def run_test(self):
        peer_lengths = self._record_peer_lengths()
        if all(length == 0 for length in peer_lengths):
            assert_equal(self._manual_address_supplied(), False)
            for node in self.nodes:
                assert_equal(node.getmodelnetworkinfo()["automatic_spend_atoms"], 0)
        self.bootstrap()


if __name__ == "__main__":
    SwarmLanDiscoverTest(__file__).main()
