The dynamic forward proxy cluster now publishes host additions and removals from DNS results once
per main thread event loop pass instead of once per DNS result. Each publish rebuilds the cluster's
full host set, so a cluster that resolves many new hostnames at a high rate no longer saturates the
main thread. Request routing is unaffected because host selection reads the cluster's host map,
which is still updated immediately. Membership stats and the workers' host set view may lag by at
most one event loop pass, membership change stats are counted once per publish rather than once
per DNS result, and connection pools of a removed host are drained when the pending publish runs.
Clusters with outlier detection or health checking keep publishing once per DNS result. This
behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.dfp_cluster_coalesce_host_updates`` to ``false``.
