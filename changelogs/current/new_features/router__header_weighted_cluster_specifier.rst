Added a new ``header_weighted`` cluster specifier plugin that selects the
target upstream cluster by combining header-based explicit selection with
weighted random choice. When the configured request header (e.g.
``x-target-cluster``) is present and non-empty, its value is used directly
as the cluster name. When the header is absent or empty, a cluster is
selected from the configured list by weighted random choice (same algorithm
as the built-in weighted_clusters). When no cluster can be selected (total
weight is zero), the configured ``default_cluster`` is used as fallback.

Extension name: ``envoy.router.cluster_specifier_plugin.header_weighted``
(alpha).
