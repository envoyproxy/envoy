Added support for ``refreshRouteCluster`` on weighted cluster routes. When a filter calls
``refreshRouteCluster()``, the weighted cluster entry will select a different cluster from the
configured pool, avoiding previously-tried clusters within the same request. Once all clusters
have been tried, the selection pool resets so that any cluster may be chosen again. This enables
filters to implement per-attempt cluster failover across weighted clusters without replacing the
entire route.
