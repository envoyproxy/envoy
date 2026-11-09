Added the runtime guard ``envoy.reloadable_features.lrs_skip_idle_clusters`` (disabled by default). When enabled,
the load stats reporter no longer visits the hosts of clusters that had no load to report since the last report,
which removes most of the cost of load reporting when many of the clusters selected by the LRS server are idle.
The reports are the same either way. Independently of the guard, the reporter no longer copies the cluster map on
every report.
