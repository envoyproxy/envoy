Added ``cluster_manager.odcds_missing`` and ``cluster_manager.odcds_timeout`` counters for
on-demand cluster discovery. Each counter increments once per completed cluster discovery,
regardless of how many requests were waiting for that cluster.
