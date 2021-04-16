1.18.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* config: the v2 xDS API is no longer supported by the Envoy binary.
* grpc_stats: the default value for :ref:`stats_for_all_methods <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.stats_for_all_methods>` is switched from true to false, in order to avoid possible memory exhaustion due to an untrusted downstream sending a large number of unique method names. The previous default value was deprecated in version 1.14.0. This only changes the behavior when the value is not set. The previous behavior can be used by setting the value to true. This behavior change by be overridden by setting runtime feature `envoy.deprecated_features.grpc_stats_filter_enable_stats_for_all_methods_by_default`.
* http: fixing a standards compliance issue with :scheme. The :scheme header sent upstream is now based on the original URL scheme, rather than set based on the security of the upstream connection. This behavior can be temporarily reverted by setting `envoy.reloadable_features.preserve_downstream_scheme` to false.
* http: http3 is now enabled/disabled via build option `--define http3=disabled` rather than the extension framework. Behavior is the same, but builds may be affected for platforms or build configurations where http3 is not supported.
* http: resolving inconsistencies between :scheme and X-Forwarded-Proto. :scheme will now be set for all HTTP/1.1 requests. This changes the behavior of the gRPC access logger, Wasm filters, CSRF filter and oath2 filter for HTTP/1 traffic, where :scheme was previously not set. This change also validates that for front-line Envoys (Envoys configured with  :ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>` set to 0 and :ref:`use_remote_address <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.use_remote_address>` set to true) that HTTP/1.1 https schemed requests can not be sent over non-TLS connections. All behavioral changes listed here can be temporarily reverted by setting `envoy.reloadable_features.add_and_validate_scheme_header` to false.
* http: when a protocol error is detected in response from upstream, Envoy sends 502 BadGateway downstream and access log entry contains UPE flag. This behavior change can be overwritten to use error code 503 by setting `envoy.reloadable_features.return_502_for_upstream_protocol_errors` to false.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* access_logs: change command operator %UPSTREAM_CLUSTER% to resolve to :ref:`alt_stat_name <envoy_v3_api_field_config.cluster.v3.Cluster.alt_stat_name>` if provided. This behavior can be reverted by disabling the runtime feature `envoy.reloadable_features.use_observable_cluster_name`.
* access_logs: fix substition formatter to recognize commands ending with an integer such as DOWNSTREAM_PEER_FINGERPRINT_256.
* access_logs: set the error flag `NC` for `no cluster found` instead of `NR` if the route is found but the corresponding cluster is not available.
* admin: added :ref:`observability_name <envoy_v3_api_field_admin.v3.ClusterStatus.observability_name>` information to GET /clusters?format=json :ref:`cluster status <envoy_v3_api_msg_admin.v3.ClusterStatus>`.
* dns: both the :ref:`strict DNS <arch_overview_service_discovery_types_strict_dns>` and
  :ref:`logical DNS <arch_overview_service_discovery_types_logical_dns>` cluster types now honor the
  :ref:`hostname <envoy_v3_api_field_config.endpoint.v3.Endpoint.hostname>` field if not empty.
  Previously resolved hosts would have their hostname set to the configured DNS address for use with
  logging, :ref:`auto_host_rewrite <envoy_api_field_route.RouteAction.auto_host_rewrite>`, etc.
  Setting the hostname manually allows overriding the internal hostname used for such features while
  still allowing the original DNS resolution name to be used.
* grpc_json_transcoder: filter now adheres to encoder and decoder buffer limits. Requests and responses
  that require buffering over the limits will be directly rejected. The behavior can be reverted by
  disabling runtime feature `envoy.reloadable_features.grpc_json_transcoder_adhere_to_buffer_limits`.
  To reduce or increase the buffer limits the filter adheres to, reference the :ref:`flow control documentation <faq_flow_control>`.
* hds: support custom health check port via :ref:`health_check_config <envoy_v3_api_msg_config.endpoint.v3.endpoint.healthcheckconfig>`.
* healthcheck: the :ref:`health check filter <config_http_filters_health_check>` now sends the
  :ref:`x-envoy-immediate-health-check-fail <config_http_filters_router_x-envoy-immediate-health-check-fail>` header
  for all responses when Envoy is in the health check failed state. Additionally, receiving the
  :ref:`x-envoy-immediate-health-check-fail <config_http_filters_router_x-envoy-immediate-health-check-fail>`
  header (either in response to normal traffic or in response to an HTTP :ref:`active health check <arch_overview_health_checking>`) will
  cause Envoy to immediately :ref:`exclude <arch_overview_load_balancing_excluded>` the host from
  load balancing calculations. This has the useful property that such hosts, which are being
  explicitly told to disable traffic, will not be counted for panic routing calculations. See the
  excluded documentation for more information. This behavior can be temporarily reverted by setting
  the `envoy.reloadable_features.health_check.immediate_failure_exclude_from_cluster` feature flag
  to false. Note that the runtime flag covers *both* the health check filter responding with
  `x-envoy-immediate-health-check-fail` in all cases (versus just non-HC requests) as well as
  whether receiving `x-envoy-immediate-health-check-fail` will cause exclusion or not. Thus,
  depending on the Envoy deployment, the feature flag may need to be flipped on both downstream
  and upstream instances, depending on the reason.
* http: allow to use path canonicalizer from `googleurl <https://quiche.googlesource.com/googleurl>`_
  instead of `//source/common/chromium_url`. The new path canonicalizer is enabled by default. To
  revert to the legacy path canonicalizer, enable the runtime flag
  `envoy.reloadable_features.remove_forked_chromium_url`.
* http: increase the maximum allowed number of initial connection WINDOW_UPDATE frames sent by the peer from 1 to 5.
* http: no longer adding content-length: 0 for requests which should not have bodies. This behavior can be temporarily reverted by setting `envoy.reloadable_features.dont_add_content_length_for_bodiless_requests` false.
* http: port stripping now works for CONNECT requests, though the port will be restored if the CONNECT request is sent upstream. This behavior can be temporarily reverted by setting `envoy.reloadable_features.strip_port_from_connect` to false.
* http: upstream flood and abuse checks increment the count of opened HTTP/2 streams when Envoy sends
  initial HEADERS frame for the new stream. Before the counter was incrementred when Envoy received
  response HEADERS frame with the END_HEADERS flag set from upstream server.
* lua: added function `timestamp` to provide millisecond resolution timestamps by passing in `EnvoyTimestampResolution.MILLISECOND`.
* oauth filter: added the optional parameter :ref:`auth_scopes <envoy_v3_api_field_extensions.filters.http.oauth2.v3alpha.OAuth2Config.auth_scopes>` with default value of 'user' if not provided. Enables this value to be overridden in the Authorization request to the OAuth provider.
* perf: allow reading more bytes per operation from raw sockets to improve performance.
* router: extended custom date formatting to DOWNSTREAM_PEER_CERT_V_START and DOWNSTREAM_PEER_CERT_V_END when using :ref:`custom request/response header formats <config_http_conn_man_headers_custom_request_headers>`.
* router: made the path rewrite available without finalizing headers, so the filter could calculate the current value of the final url.
* tracing: added `upstream_cluster.name` tag that resolves to resolve to :ref:`alt_stat_name <envoy_v3_api_field_config.cluster.v3.Cluster.alt_stat_name>` if provided (and otherwise the cluster name).
* udp: configuration has been added for :ref:`GRO <envoy_v3_api_field_config.core.v3.UdpSocketConfig.prefer_gro>`
  which used to be force enabled if the OS supports it. The default is now disabled for server
  sockets and enabled for client sockets (see the new features section for links).
* upstream: host weight changes now cause a full load balancer rebuild as opposed to happening
  atomically inline. This change has been made to support load balancer pre-computation of data
  structures based on host weight, but may have performance implications if host weight changes
  are very frequent. This change can be disabled by setting the `envoy.reloadable_features.upstream_host_weight_change_causes_rebuild`
  feature flag to false. If setting this flag to false is required in a deployment please open an
  issue against the project.
1.18.2 (April 15, 2021)
=======================

Bug Fixes
---------

code: fixed more build issues on our path to a glorious release.
