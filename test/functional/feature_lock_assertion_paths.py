#!/usr/bin/env python3
# Copyright (c) 2026 The BTX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Node paths that ran into their own lock assertions.

- invalidateblock delivers ActiveTipChange after InvalidateBlock has
  released cs_main.

A DEBUG_LOCKORDER build used to abort there. Every build must answer
and keep running.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet_util import generate_keypair

INLINE_SIGNER_WARNING = (
    "Warning: -matmulattestationsignerkey exposes an online signing key "
    "through process/config surfaces; use a permission-restricted "
    "-matmulattestationsignerkeyfile."
)


class LockAssertionPathsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        signer_wif, signer_pub = generate_keypair(wif=True)
        self.extra_args = [[
            f"-matmultrustedpubkey={signer_pub.hex()}",
            "-matmultrustedthreshold=1",
            f"-matmulattestationsignerkey={signer_wif}",
            "-matmulattestationserve=1",
            "-matmulvalidation=consensus",
            "-debug=net",
        ]]

    def run_test(self):
        node = self.nodes[0]
        self.generate(node, 3)
        tip = node.getbestblockhash()

        self.log.info("invalidateblock and reconsiderblock the tip")
        node.invalidateblock(tip)
        assert_equal(node.getblockcount(), 2)
        node.reconsiderblock(tip)
        assert_equal(node.getbestblockhash(), tip)

        self.stop_node(0, expected_stderr=INLINE_SIGNER_WARNING)


if __name__ == '__main__':
    LockAssertionPathsTest(__file__).main()
