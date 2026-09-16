Fixed use-after-free crashes and resource leaks when a Redis cluster is removed. The discovery
session could outlive the cluster (its discovery clients hold a reference to it) with the resolve
timer still armed, and in-flight hostname resolutions and zone-discovery ``INFO`` requests were
never cancelled. Cluster teardown now explicitly shuts the discovery session down, cancelling all
in-flight discovery work and closing the discovery connections.
