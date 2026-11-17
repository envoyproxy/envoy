Added the runtime guard ``envoy.restart_features.enable_optimized_lrs_bookkeeping`` (disabled by default).
When enabled, the load stats reporter no longer copies the cluster map on every report.
