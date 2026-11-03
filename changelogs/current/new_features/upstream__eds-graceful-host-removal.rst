Added :ref:`graceful_host_removal
<envoy_v3_api_field_config.cluster.v3.Cluster.EdsClusterConfig.graceful_host_removal>` to retain
healthy removed EDS hosts as ``PENDING_DYNAMIC_REMOVAL`` while replacement endpoints warm up via
active health checking until ``healthy_panic_threshold`` is met or initial health checks complete.
