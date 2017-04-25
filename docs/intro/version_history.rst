Version history
---------------

1.2.0
=====

* :ref:`Cluster discovery service (CDS) API <config_cluster_manager_cds>`.
* :ref:`Outlier detection <arch_overview_outlier_detection>` (passive health checking).
* Envoy configuration is now checked against a :ref:`JSON schema <config_overview>`.
* :ref:`Ring hash <arch_overview_load_balancing_types>` consistent load balancer, as well as HTTP
  consistent hash routing :ref:`based on a policy <config_http_conn_man_route_table_hash_policy>`.
* Vastly :ref:`enhanced global rate limit configuration <arch_overview_rate_limit>` via the HTTP
  rate limiting filter.
* HTTP routing to a cluster :ref:`retrieved from a header
  <config_http_conn_man_route_table_route_cluster_header>`.
* :ref:`Weighted cluster <config_http_conn_man_route_table_route_config_weighted_clusters>` HTTP
  routing.
* :ref:`Auto host rewrite <config_http_conn_man_route_table_route_auto_host_rewrite>` during HTTP
  routing.
* :ref:`Regex header matching <config_http_conn_man_route_table_route_headers>` during HTTP routing.
* HTTP access log :ref:`runtime filter <config_http_con_manager_access_log_filters_runtime>`.
* LightStep tracer :ref:`parent/child span association <arch_overview_tracing>`.
* :ref:`Route discovery service (RDS) API <config_http_conn_man_rds>`.
* HTTP router :ref:`x-envoy-upstream-rq-timeout-alt-response header
  <config_http_filters_router_x-envoy-upstream-rq-timeout-alt-response>` support.
* *use_original_dst* and *bind_to_port* :ref:`listener options <config_listeners>` (useful for
  iptables based transparent proxy support).
* TCP proxy filter :ref:`route table support <config_network_filters_tcp_proxy>`.
* Configurable :ref:`stats flush interval <config_overview_stats_flush_interval_ms>`.
* Various :ref:`third party library upgrades <install_requirements>`, including using BoringSSL as
  the default SSL provider.
* No longer maintain closed HTTP/2 streams for priority calculations. Leads to substantial memory
  savings for large meshes.
* Numerous small changes and fixes not listed here.

1.1.0
=====

* Switch from Jannson to RapidJSON for our JSON library (allowing for a configuration schema in
  1.2.0).
* Upgrade :ref:`recommended version <install_requirements>` of various other libraries.
* :ref:`Configurable DNS refresh rate <config_cluster_manager_cluster_dns_refresh_rate_ms>` for
  DNS service discovery types.
* Upstream circuit breaker configuration can be :ref:`overridden via runtime
  <config_cluster_manager_cluster_runtime>`.
* :ref:`Zone aware routing support <arch_overview_load_balancing_zone_aware_routing>`.
* Generic :ref:`header matching routing rule <config_http_conn_man_route_table_route_headers>`.
* HTTP/2 :ref:`graceful connection draining <config_http_conn_man_drain_timeout_ms>` (double
  GOAWAY).
* DynamoDB filter :ref:`per shard statistics <config_http_filters_dynamo>` (pre-release AWS
  feature).
* Initial release of the :ref:`fault injection HTTP filter <config_http_filters_fault_injection>`.
* HTTP :ref:`rate limit filter <config_http_filters_rate_limit>` enhancements (note that the
  configuration for HTTP rate limiting is going to be overhauled in 1.2.0).
* Added :ref:`refused-stream retry policy <config_http_filters_router_x-envoy-retry-on>`.
* Multiple :ref:`priority queues <arch_overview_http_routing_priority>` for upstream clusters
  (configurable on a per route basis, with separate connection pools, circuit breakers, etc.).
* Added max connection circuit breaking to the :ref:`TCP proxy filter <arch_overview_tcp_proxy>`.
* Added :ref:`CLI <operations_cli>` options for setting the logging file flush interval as well
  as the drain/shutdown time during hot restart.
* A very large number of performance enhancements for core HTTP/TCP proxy flows as well as a
  few new configuration flags to allow disabling expensive features if they are not needed
  (specifically request ID generation and dynamic response code stats).
* Support Mongo 3.2 in the :ref:`Mongo sniffing filter <config_network_filters_mongo_proxy>`.
* Lots of other small fixes and enhancements not listed.

1.0.0
=====

Initial open source release.
