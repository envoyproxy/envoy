.. _deprecated:

Deprecated
----------

As of release 1.3.0, Envoy will follow a
`Breaking Change Policy <https://github.com/envoyproxy/envoy/blob/master//CONTRIBUTING.md#breaking-change-policy>`_.

The following features have been DEPRECATED and will be removed in the specified release cycle.
A logged warning is expected for each deprecated item that is in deprecation window.
Deprecated items below are listed in chronological order.

Version 1.12.0 (pending)
========================
* The ORIGINAL_DST_LB :ref:`load balancing policy <envoy_api_field_Cluster.lb_policy>` is
  deprecated, use CLUSTER_PROVIDED policy instead when configuring an :ref:`original destination
  cluster <envoy_api_field_Cluster.type>`.
* The `regex` field in :ref:`StringMatcher <envoy_api_msg_type.matcher.StringMatcher>` has been
  deprecated in favor of the `safe_regex` field.
* The `regex` field in :ref:`RouteMatch <envoy_api_msg_route.RouteMatch>` has been
  deprecated in favor of the `safe_regex` field.
* The `allow_origin` and `allow_origin_regex` fields in :ref:`CorsPolicy
  <envoy_api_msg_route.CorsPolicy>` have been deprecated in favor of the
  `allow_origin_string_match` field.
* The `pattern` and `method` fields in :ref:`VirtualCluster <envoy_api_msg_route.VirtualCluster>`
  have been deprecated in favor of the `headers` field.
* The `regex_match` field in :ref:`HeaderMatcher <envoy_api_msg_route.HeaderMatcher>` has been
  deprecated in favor of the `safe_regex_match` field.
* The `value` and `regex` fields in :ref:`QueryParameterMatcher
  <envoy_api_msg_route.QueryParameterMatcher>` has been deprecated in favor of the `string_match`
  and `present_match` fields.
* The :option:`--allow-unknown-fields` command-line option,
  use :option:`--allow-unknown-static-fields` instead.
* The use of HTTP_JSON_V1 :ref:`Zipkin collector endpoint version
  <envoy_api_field_config.trace.v2.ZipkinConfig.collector_endpoint_version>` or not explicitly
  specifying it is deprecated, use HTTP_JSON or HTTP_PROTO instead.
* The `operation_name` field in :ref:`HTTP connection manager
  <envoy_api_msg_config.filter.network.http_connection_manager.v2.HttpConnectionManager>`
  has been deprecated in favor of the `traffic_direction` field in
  :ref:`Listener <envoy_api_msg_Listener>`. The latter takes priority if
  specified.
* The `use_http2` field in
  :ref:`HTTP health checker <envoy_api_msg_core.HealthCheck.HttpHealthCheck>` has been deprecated in
  favor of the `codec_client_type` field.
* The use of :ref:`gRPC bridge filter <config_http_filters_grpc_bridge>` for
  gRPC stats has been deprecated in favor of the dedicated :ref:`gRPC stats
  filter <config_http_filters_grpc_stats>`

Version 1.11.2 (October 8, 2019)
================================
* Use of :ref:`idle_timeout
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.idle_timeout>`
  is deprecated. Use :ref:`common_http_protocol_options
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.common_http_protocol_options>`
  instead.

Version 1.11.0 (July 11, 2019)
==============================
* The --max-stats and --max-obj-name-len flags no longer has any effect.
* Use of :ref:`cluster <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.cluster>` in :ref:`redis_proxy.proto <envoy_api_file_envoy/config/filter/network/redis_proxy/v2/redis_proxy.proto>` is deprecated. Set a :ref:`catch_all_route <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.PrefixRoutes.catch_all_route>` instead.
* Use of :ref:`catch_all_cluster <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.PrefixRoutes.catch_all_cluster>` in :ref:`redis_proxy.proto <envoy_api_file_envoy/config/filter/network/redis_proxy/v2/redis_proxy.proto>` is deprecated. Set a :ref:`catch_all_route <envoy_api_field_config.filter.network.redis_proxy.v2.RedisProxy.PrefixRoutes.catch_all_route>` instead.
* Use of json based schema in router check tool tests. The tests should follow validation :repo:`schema<test/tools/router_check/validation.proto>`.
* Use of the v1 style route configuration for the :ref:`TCP proxy filter <config_network_filters_tcp_proxy>`
  is now fully replaced with listener :ref:`filter chain matching <envoy_api_msg_listener.FilterChainMatch>`.
  Use this instead.
* Use of :ref:`runtime <envoy_api_field_config.bootstrap.v2.Bootstrap.runtime>` in :ref:`Bootstrap
  <envoy_api_msg_config.bootstrap.v2.Bootstrap>`. Use :ref:`layered_runtime
  <envoy_api_field_config.bootstrap.v2.Bootstrap.layered_runtime>` instead.
* Specifying "deprecated_v1: true" in HTTP and network filter configuration to allow loading JSON
  configuration is now deprecated and will be removed in a following release. Update any custom
  filters to use protobuf configuration. A struct can be used for a mostly 1:1 conversion if needed.
  The `envoy.deprecated_features.v1_filter_json_config` runtime key can be used to temporarily
  enable this feature once the deprecation becomes fail by default.

Version 1.10.0 (Apr 5, 2019)
============================
* Use of `use_alpha` in :ref:`Ext-Authz Authorization Service <envoy_api_file_envoy/service/auth/v2/external_auth.proto>` is deprecated. It should be used for a short time, and only when transitioning from alpha to V2 release version.
* Use of `enabled` in `CorsPolicy`, found in
  :ref:`route.proto <envoy_api_file_envoy/api/v2/route/route.proto>`.
  Set the `filter_enabled` field instead.
* Use of the `type` field in the `FaultDelay` message (found in
  :ref:`fault.proto <envoy_api_file_envoy/config/filter/fault/v2/fault.proto>`)
  has been deprecated. It was never used and setting it has no effect. It will be removed in the
  following release.

Version 1.9.0 (Dec 20, 2018)
============================
* Order of execution of the network write filter chain has been reversed. Prior to this release cycle it was incorrect, see `#4599 <https://github.com/envoyproxy/envoy/issues/4599>`_. In the 1.9.0 release cycle we introduced `bugfix_reverse_write_filter_order` in `lds.proto <https://github.com/envoyproxy/envoy/blob/master/api/envoy/api/v2/lds.proto>`_ to temporarily support both old and new behaviors. Note this boolean field is deprecated.
* Order of execution of the HTTP encoder filter chain has been reversed. Prior to this release cycle it was incorrect, see `#4599 <https://github.com/envoyproxy/envoy/issues/4599>`_. In the 1.9.0 release cycle we introduced `bugfix_reverse_encode_order` in `http_connection_manager.proto <https://github.com/envoyproxy/envoy/blob/master/api/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.proto>`_ to temporarily support both old and new behaviors. Note this boolean field is deprecated.
* Use of the v1 REST_LEGACY ApiConfigSource is deprecated.
* Use of std::hash in the ring hash load balancer is deprecated.
* Use of `rate_limit_service` configuration in the `bootstrap configuration <https://github.com/envoyproxy/envoy/blob/master/api/envoy/config/bootstrap/v2/bootstrap.proto>`_ is deprecated.
* Use of `runtime_key` in `RequestMirrorPolicy`, found in
  `route.proto <https://github.com/envoyproxy/envoy/blob/master/api/envoy/api/v2/route/route.proto>`_
  is deprecated. Set the `runtime_fraction` field instead.
* Use of buffer filter `max_request_time` is deprecated in favor of the request timeout found in `HttpConnectionManager <https://github.com/envoyproxy/envoy/blob/master/api/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.proto>`_

Version 1.8.0 (Oct 4, 2018)
==============================
* Use of the v1 API (including `*.deprecated_v1` fields in the v2 API) is deprecated.
  See envoy-announce `email <https://groups.google.com/forum/#!topic/envoy-announce/oPnYMZw8H4U>`_.
* Use of the legacy
  `ratelimit.proto <https://github.com/envoyproxy/envoy/blob/b0a518d064c8255e0e20557a8f909b6ff457558f/source/common/ratelimit/ratelimit.proto>`_
  is deprecated, in favor of the proto defined in
  `date-plane-api <https://github.com/envoyproxy/envoy/blob/master/api/envoy/service/ratelimit/v2/rls.proto>`_
  Prior to 1.8.0, Envoy can use either proto to send client requests to a ratelimit server with the use of the
  `use_data_plane_proto` boolean flag in the `ratelimit configuration <https://github.com/envoyproxy/envoy/blob/master/api/envoy/config/ratelimit/v2/rls.proto>`_.
  However, when using the deprecated client a warning is logged.
* Use of the --v2-config-only flag.
* Use of both `use_websocket` and `websocket_config` in
  `route.proto <https://github.com/envoyproxy/envoy/blob/master/api/envoy/api/v2/route/route.proto>`_
  is deprecated. Please use the new `upgrade_configs` in the
  `HttpConnectionManager <https://github.com/envoyproxy/envoy/blob/master/api/envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.proto>`_
  instead.
* Use of the integer `percent` field in `FaultDelay <https://github.com/envoyproxy/envoy/blob/master/api/envoy/config/filter/fault/v2/fault.proto>`_
  and in `FaultAbort <https://github.com/envoyproxy/envoy/blob/master/api/envoy/config/filter/http/fault/v2/fault.proto>`_ is deprecated in favor
  of the new `FractionalPercent` based `percentage` field.
* Setting hosts via `hosts` field in `Cluster` is deprecated. Use `load_assignment` instead.
* Use of `response_headers_to_*` and `request_headers_to_add` are deprecated at the `RouteAction`
  level. Please use the configuration options at the `Route` level.
* Use of `runtime` in `RouteMatch`, found in
  `route.proto <https://github.com/envoyproxy/envoy/blob/master/api/envoy/api/v2/route/route.proto>`_.
  Set the `runtime_fraction` field instead.
* Use of the string `user` field in `Authenticated` in `rbac.proto <https://github.com/envoyproxy/envoy/blob/master/api/envoy/config/rbac/v2alpha/rbac.proto>`_
  is deprecated in favor of the new `StringMatcher` based `principal_name` field.

Version 1.7.0 (Jun 21, 2018)
===============================
* Admin mutations should be sent as POSTs rather than GETs. HTTP GETs will result in an error
  status code and will not have their intended effect. Prior to 1.7, GETs can be used for
  admin mutations, but a warning is logged.
* Rate limit service configuration via the `cluster_name` field is deprecated. Use `grpc_service`
  instead.
* gRPC service configuration via the `cluster_names` field in `ApiConfigSource` is deprecated. Use
  `grpc_services` instead. Prior to 1.7, a warning is logged.
* Redis health checker configuration via the `redis_health_check` field in `HealthCheck` is
  deprecated. Use `custom_health_check` with name `envoy.health_checkers.redis` instead. Prior
  to 1.7, `redis_health_check` can be used, but warning is logged.
* `SAN` is replaced by `URI` in the `x-forwarded-client-cert` header.
* The `endpoint` field in the http health check filter is deprecated in favor of the `headers`
  field where one can specify HeaderMatch objects to match on.
* The `sni_domains` field in the filter chain match was deprecated/renamed to `server_names`.

Version 1.6.0 (March 20, 2018)
=================================
* DOWNSTREAM_ADDRESS log formatter is deprecated. Use DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT
  instead.
* CLIENT_IP header formatter is deprecated. Use DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT instead.
* 'use_original_dst' field in the v2 LDS API is deprecated. Use listener filters and filter chain
  matching instead.
* `value` and `regex` fields in the `HeaderMatcher` message is deprecated. Use the `exact_match`
  or `regex_match` oneof instead.

Version 1.5.0 (Dec 4, 2017)
==============================
* The outlier detection `ejections_total` stats counter has been deprecated and not replaced. Monitor
  the individual `ejections_detected_*` counters for the detectors of interest, or
  `ejections_enforced_total` for the total number of ejections that actually occurred.
* The outlier detection `ejections_consecutive_5xx` stats counter has been deprecated in favour of
  `ejections_detected_consecutive_5xx` and `ejections_enforced_consecutive_5xx`.
* The outlier detection `ejections_success_rate` stats counter has been deprecated in favour of
  `ejections_detected_success_rate` and `ejections_enforced_success_rate`.

Version 1.4.0 (Aug 24, 2017)
============================
* Config option `statsd_local_udp_port` has been deprecated and has been replaced with
  `statsd_udp_ip_address`.
* `HttpFilterConfigFactory` filter API has been deprecated in favor of `NamedHttpFilterConfigFactory`.
* Config option `http_codec_options` has been deprecated and has been replaced with `http2_settings`.
* The following log macros have been deprecated: `log_trace`, `log_debug`, `conn_log`,
  `conn_log_info`, `conn_log_debug`, `conn_log_trace`, `stream_log`, `stream_log_info`,
  `stream_log_debug`, `stream_log_trace`. For replacements, please see
  `logger.h <https://github.com/envoyproxy/envoy/blob/master/source/common/common/logger.h>`_.
* The connectionId() and ssl() callbacks of StreamFilterCallbacks have been deprecated and
  replaced with a more general connection() callback, which, when not returning a nullptr, can be
  used to get the connection id and SSL connection from the returned Connection object pointer.
* The protobuf stub gRPC support via `Grpc::RpcChannelImpl` is now replaced with `Grpc::AsyncClientImpl`.
  This no longer uses `protoc` generated stubs but instead utilizes C++ template generation of the
  RPC stubs. `Grpc::AsyncClientImpl` supports streaming, in addition to the previous unary, RPCs.
* The direction of network and HTTP filters in the configuration will be ignored from 1.4.0 and
  later removed from the configuration in the v2 APIs. Filter direction is now implied at the C++ type
  level. The `type()` methods on the `NamedNetworkFilterConfigFactory` and
  `NamedHttpFilterConfigFactory` interfaces have been removed to reflect this.
