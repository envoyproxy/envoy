On retry attempts, the router now refreshes load-balancer metadata match criteria, upstream
``:scheme``, and router-managed ``auto_sni`` / ``auto_san_validation`` transport socket options
(clearing stale router-applied SNI/SAN entries on non-hedged retries), and supports
``refreshRouteCluster()`` on ``cluster_header`` routes when ``refreshClusterOnRetry()`` is enabled.
This behavior can be temporarily reverted by setting the runtime flag
``envoy.reloadable_features.router_refresh_transport_and_lb_on_retry`` to ``false``.
