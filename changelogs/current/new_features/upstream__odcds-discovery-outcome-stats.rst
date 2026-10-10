Added ``cluster_manager.odcds_attempt``, ``cluster_manager.odcds_success``,
``cluster_manager.odcds_missing``, and ``cluster_manager.odcds_timeout`` counters for on-demand
cluster discovery. Each counter increments once per discovery, regardless of how many requests
were waiting for that cluster.
