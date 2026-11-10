Added ``cluster_manager.odcds.discovery_attempt``, ``cluster_manager.odcds.discovery_success``,
``cluster_manager.odcds.discovery_missing``, and ``cluster_manager.odcds.discovery_timeout``
counters for :ref:`on-demand cluster discovery <config_cluster_manager_odcds_stats>`. Each counter
increments once per discovery, regardless of how many requests were waiting for that cluster.
