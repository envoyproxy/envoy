On-demand cluster discovery (ODCDS) can now answer repeated requests for a cluster the server has
already reported as missing immediately from that remembered answer, instead of starting another
discovery and waiting for the response or the discovery timeout. The answer is remembered per
config source, only for explicitly requested clusters, and is withdrawn as soon as an update
delivers the cluster. Requests answered this way are counted in the new
:ref:`cluster_manager.odcds.known_missing_answers <config_cluster_manager_odcds_stats>` counter.
This behavior is off by default and can be enabled by setting the runtime guard
``envoy.reloadable_features.odcds_missing_cluster_cache`` to ``true``.
