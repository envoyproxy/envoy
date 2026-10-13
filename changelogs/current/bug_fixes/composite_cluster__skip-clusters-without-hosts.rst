Fixed :ref:`composite clusters <arch_overview_composite_cluster>` failing requests with ``503
no_healthy_upstream`` when the sub-cluster selected for an attempt has no host available, for
example because its endpoint list is empty or because all of its hosts have been ejected. The
composite cluster now fails over to the following sub-clusters in the configured list within the
same attempt. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.composite_cluster_skip_clusters_without_hosts`` to ``false``.
