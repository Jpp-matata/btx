# BTX 0.35.0 release candidate

This branch reports `v0.35.0rc1` (`CLIENT_VERSION_MINOR` 35, `CLIENT_VERSION_BUILD` 0, `CLIENT_VERSION_RC` 1, `CLIENT_VERSION_IS_RELEASE` true). It is not the shipping tag `v0.35.0`. The last shipping tag remains **v0.34.15**. Notes for that tag stay in [release-notes-0.34.15.md](release-notes-0.34.15.md).

The candidate keeps the monetary node and adds the swarm as a thread inside `btxd` when Modelnet is compiled. `-modelnet` defaults on. `-modelnet=0` leaves the monetary node running. The thread does not spend. `automatic_spend_atoms` stays 0. `btxd` does not start `btx-swarmd` or `btx-paynetd`.

The public map of the candidate is [doc/modelnet/swarm/00_THIS_TREE.md](../modelnet/swarm/00_THIS_TREE.md). Product behavior and the work that remains are in [01_PRODUCT_AND_DEFAULT_LIFECYCLE.md](../modelnet/swarm/01_PRODUCT_AND_DEFAULT_LIFECYCLE.md) and [14_PERFORMANCE_SCALE_AND_ACCEPTANCE_METHODOLOGY.md](../modelnet/swarm/14_PERFORMANCE_SCALE_AND_ACCEPTANCE_METHODOLOGY.md).

This candidate is not release-qualified. A passing in-process suite is not a second host, a second GPU, Windows, macOS, or a sealed acceptance run.
