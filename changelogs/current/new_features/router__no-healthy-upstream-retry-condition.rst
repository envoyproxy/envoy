Added ``no-healthy-upstream`` as a new :ref:`retry condition
<config_http_filters_router_x-envoy-retry-on>` for the router filter. When configured, Envoy will
retry a request if the selected cluster has no healthy upstream hosts available (the load balancer
cannot select a host). This is essential for :ref:`composite clusters
<arch_overview_composite_cluster>` where a sub-cluster may have zero endpoints due to DNS
resolution failure or outlier ejection, enabling per-request failover to the next sub-cluster
instead of returning an immediate 503.
