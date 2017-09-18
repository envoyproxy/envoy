Version history
---------------

1.4.0
=====

* macOS is :repo:`now supported </bazel#quick-start-bazel-build-for-developers>`. (A few features
  are missing such as hot restart and original destination routing).
* YAML is now directly supported for :ref:`config files <config_overview>`.
* Added :ref:`/routes <operations_admin_interface_routes>` admin endpoint.
* End-to-end flow control is now supported for TCP proxy, HTTP/1, and HTTP/2. HTTP flow control
  that includes filter buffering is incomplete and will be implemented in 1.5.0.
* Log verbosity :repo:`compile time flag </bazel#log-verbosity>` added.
* Hot restart :repo:`compile time flag </bazel#hot-restart>` added.
* Original destination :ref:`cluster <arch_overview_service_discovery_types_original_destination>`
  and :ref:`load balancer <arch_overview_load_balancing_types_original_destination>` added.
* :ref:`WebSocket <arch_overview_websocket>` is now supported.
* Virtual cluster priorities have been hard removed without deprecation as we are reasonably sure
  no one is using this feature.
* Route :ref:`validate_clusters <config_http_conn_man_route_table_validate_clusters>` option added.
* :ref:`x-envoy-downstream-service-node <config_http_conn_man_headers_downstream-service-node>`
  header added.
* :ref:`x-forwarded-client-cert <config_http_conn_man_headers_x-forwarded-client-cert>` header
  added.
* Initial HTTP/1 forward proxy support for :ref:`absolute URLs
  <config_http_conn_man_http1_settings>` has been added.
* HTTP/2 codec settings are now :ref:`configurable <config_http_conn_man_http2_settings>`.
* gRPC/JSON transcoder :ref:`filter <config_http_filters_grpc_json_transcoder>` added.
* gRPC web :ref:`filter <config_http_filters_grpc_web>` added.
* Configurable timeout for the rate limit service call in the :ref:`network
  <config_network_filters_rate_limit>` and :ref:`HTTP <config_http_filters_rate_limit>` rate limit
  filters.
* :ref:`x-envoy-retry-grpc-on <config_http_filters_router_x-envoy-retry-grpc-on>` header added.
* :ref:`LDS API <arch_overview_dynamic_config_lds>` added.
* TLS :ref:`require_client_certificate <config_listener_ssl_context_require_client_certificate>`
  option added.
* :ref:`Configuration check tool <install_tools_config_load_check_tool>` added.
* :ref:`JSON schema check tool <install_tools_schema_validator_check_tool>` added.
* Config validation mode added via the :option:`--mode` option.
* :option:`--local-address-ip-version` option added.
* IPv6 support is now complete.
* UDP :ref:`statsd_ip_address <config_overview_statsd_udp_ip_address>` option added.
* Per-cluster :ref:`DNS resolvers <config_cluster_manager_cluster_dns_resolvers>` added.
* :ref:`Fault filter <config_http_filters_fault_injection>` enhancements and fixes.
* Several features are :repo:`deprecated as of the 1.4.0 release </DEPRECATED.md#version-140>`. They
  will be removed at the beginning of the 1.5.0 release cycle. We explicitly call out that the
  `HttpFilterConfigFactory` filter API has been deprecated in favor of
  `NamedHttpFilterConfigFactory`.
* Many small bug fixes and performance improvements not listed.

1.3.0
=====

* As of this release, we now have an official :repo:`breaking change policy
  </CONTRIBUTING.md#breaking-change-policy>`. Note that there are numerous breaking configuration
  changes in this release. They are not listed here. Future releases will adhere to the policy and
  have clear documentation on deprecations and changes.
* Bazel is now the canonical build system (replacing CMake). There have been a huge number of
  changes to the development/build/test flow. See :repo:`/bazel/README.md` and
  :repo:`/ci/README.md` for more information.
* :ref:`Outlier detection <arch_overview_outlier_detection>` has been expanded to include success
  rate variance, and all parameters are now configurable in both runtime and in the JSON
  configuration.
* TCP level :ref:`listener <config_listeners_per_connection_buffer_limit_bytes>` and
  :ref:`cluster <config_cluster_manager_cluster_per_connection_buffer_limit_bytes>` connections now
  have configurable receive buffer limits at which point connection level back pressure is applied.
  Full end to end flow control will be available in a future release.
* :ref:`Redis health checking <config_cluster_manager_cluster_hc>` has been added as an active
  health check type. Full Redis support will be documented/supported in 1.4.0.
* :ref:`TCP health checking <config_cluster_manager_cluster_hc_tcp_health_checking>` now supports a
  "connect only" mode that only checks if the remote server can be connected to without
  writing/reading any data.
* `BoringSSL <https://boringssl.googlesource.com/boringssl>`_ is now the only supported TLS provider.
  The default cipher suites and ECDH curves have been updated with more modern defaults for both
  :ref:`listener <config_listener_ssl_context>` and
  :ref:`cluster <config_cluster_manager_cluster_ssl>` connections.
* The `header value match` :ref:`rate limit action
  <config_http_conn_man_route_table_rate_limit_actions>` has been expanded to include an *expect
  match* parameter.
* Route level HTTP rate limit configurations now do not inherit the virtual host level
  configurations by default. The :ref:`include_vh_rate_limits
  <config_http_conn_man_route_table_route_include_vh>` to inherit the virtual host level options if
  desired.
* HTTP routes can now add request headers on a per route and per virtual host basis via the
  :ref:`request_headers_to_add <config_http_conn_man_route_table_route_add_req_headers>` option.
* The :ref:`example configurations <install_ref_configs>` have been refreshed to demonstrate the
  latest features.
* :ref:`per_try_timeout_ms <config_http_conn_man_route_table_route_retry>` can now be configured in
  a route's retry policy in addition to via the :ref:`x-envoy-upstream-rq-per-try-timeout-ms
  <config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms>` HTTP header.
* :ref:`HTTP virtual host matching <config_http_conn_man_route_table_vhost>` now includes support
  for prefix wildcard domains (e.g., `*.lyft.com`).
* The default for tracing random sampling has been changed to 100% and is still configurable in
  :ref:`runtime <config_http_conn_man_runtime>`.
* :ref:`HTTP tracing configuration <config_http_conn_man_tracing>` has been extended to allow tags
  to be populated from arbitrary HTTP headers.
* The :ref:`HTTP rate limit filter <config_http_filters_rate_limit>` can now be applied to internal,
  external, or all requests via the `request_type` option.
* :ref:`Listener binding <config_listeners>` now requires specifying an `address` field. This can be
  used to bind a listener to both a specific address as well as a port.
* The :ref:`MongoDB filter <config_network_filters_mongo_proxy>` now emits a stat for queries that
  do not have `$maxTimeMS` set.
* The :ref:`MongoDB filter <config_network_filters_mongo_proxy>` now emits logs that are fully valid
  JSON.
* The CPU profiler output path is now :ref:`configurable <config_admin>`.
* A :ref:`watchdog system <config_overview>` has been added that can kill the server if a deadlock
  is detected.
* A :ref:`route table checking tool <install_tools_route_table_check_tool>` has been added that can
  be used to test route tables before use.
* We have added an :ref:`example repo <extending>` that shows how to compile/link a custom filter.
* Added additional cluster wide information related to outlier detection to the :ref:`/clusters
  admin endpoint <operations_admin_interface>`.
* Multiple SANs can now be verified via the :ref:`verify_subject_alt_name
  <config_listener_ssl_context>` setting. Additionally, URI type SANs can be verified.
* HTTP filters can now be passed :ref:`opaque configuration
  <config_http_conn_man_route_table_opaque_config>` specified on a per route basis.
* By default Envoy now has a built in crash handler that will print a back trace. This behavior can
  be disabled if desired via the ``--define=signal_trace=disabled`` Bazel option.
* Zipkin has been added as a supported :ref:`tracing provider <arch_overview_tracing>`.
* Numerous small changes and fixes not listed here.

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
