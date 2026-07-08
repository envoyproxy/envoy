In ```OriginalDstCluster```, ```addHost()``` and ```cleanup()``` are optimized to avoid partitioning
of hosts into healthy/degraded sets, because the OriginalDstCluster's load balancer implementation
selects the exact destination address from the host map without regard for health status.

This optimization can be temporarily reverted by setting runtime guard
``envoy.reloadable_features.skip_partition_original_dst_hosts``` to ``false``.
