Fixed ``cluster_header`` routes to re-evaluate the target cluster from the configured request
header when ``refreshRouteCluster()`` is called (for example, when
``refresh_cluster_on_retry`` is enabled).
