1.19.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* access_logs: change command operator %UPSTREAM_CLUSTER% to resolve to :ref:`alt_stat_name <envoy_v3_api_field_config.cluster.v3.Cluster.alt_stat_name>` if provided. This behavior can be reverted by disabling the runtime feature `envoy.reloadable_features.use_observable_cluster_name`.
* access_logs: fix substition formatter to recognize commands ending with an integer such as DOWNSTREAM_PEER_FINGERPRINT_256.
* access_logs: set the error flag `NC` for `no cluster found` instead of `NR` if the route is found but the corresponding cluster is not available.
* admin: added :ref:`observability_name <envoy_v3_api_field_admin.v3.ClusterStatus.observability_name>` information to GET /clusters?format=json :ref:`cluster status <envoy_v3_api_msg_admin.v3.ClusterStatus>`.
* aws_request_signing: requests are now buffered by default to compute signatures which include the
  payload hash, making the filter compatible with most AWS services. Previously, requests were
  never buffered, which only produced correct signatures for requests without a body, or for
  requests to S3, ES or Glacier, which used the literal string ``UNSIGNED-PAYLOAD``. Buffering can
  be now be disabled in favor of using unsigned payloads with compatible services via the new
  `use_unsigned_payload` filter option (default false).
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
* http: added support for internal redirects with bodies. This behavior can be disabled temporarily by setting `envoy.reloadable_features.internal_redirects_with_body` to false.
* http: allow to use path canonicalizer from `googleurl <https://quiche.googlesource.com/googleurl>`_
  instead of `//source/common/chromium_url`. The new path canonicalizer is enabled by default. To
  revert to the legacy path canonicalizer, enable the runtime flag
  `envoy.reloadable_features.remove_forked_chromium_url`.
* http: increase the maximum allowed number of initial connection WINDOW_UPDATE frames sent by the peer from 1 to 5.
* http: no longer adding content-length: 0 for requests which should not have bodies. This behavior can be temporarily reverted by setting `envoy.reloadable_features.dont_add_content_length_for_bodiless_requests` false.
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

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
