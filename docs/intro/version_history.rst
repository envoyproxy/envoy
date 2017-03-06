Version history
---------------

1.2.0
=====

* CDS API
* Outlier detection
* JSON config schema
* Ring hash consistent load balancer
* Enhanced global rate limit configuration
* HTTP routing based on cluster header
* Weighted cluster HTTP routing
* Auto host rewrite during HTTP routing
* Regex header matching during HTTP routing
* HTTP access log runtime filter
* Lightstep tracer parent/child span association
* RDS API
* x-envoy-upstream-rq-timeout-alt-response haeder
* use_original_dst and bind_to_port listener options
* TCP proxy filter routing
* Configurable stats flush interval
* Various third party library upgrades, including using boringssl as the default SSL provider
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
