1.19.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* grpc_bridge_filter: the filter no longer collects grpc stats in favor of the existing grpc stats filter.
  The behavior can be reverted by changing runtime key ``envoy.reloadable_features.grpc_bridge_stats_disabled``.
* tracing: update Apache SkyWalking tracer version to be compatible with 8.4.0 data collect protocol. This change will introduce incompatibility with SkyWalking 8.3.0.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* access_log: add new access_log command operator ``%REQUEST_TX_DURATION%``.
* aws_request_signing: requests are now buffered by default to compute signatures which include the
  payload hash, making the filter compatible with most AWS services. Previously, requests were
  never buffered, which only produced correct signatures for requests without a body, or for
  requests to S3, ES or Glacier, which used the literal string ``UNSIGNED-PAYLOAD``. Buffering can
  be now be disabled in favor of using unsigned payloads with compatible services via the new
  `use_unsigned_payload` filter option (default false).
* cluster: added default value of 5 seconds for :ref:`connect_timeout <envoy_v3_api_field_config.cluster.v3.Cluster.connect_timeout>`.
* http: disable the integration between :ref:`ExtensionWithMatcher <envoy_v3_api_msg_extensions.common.matching.v3.ExtensionWithMatcher>`
  and HTTP filters by default to reflects its experimental status. This feature can be enabled by seting
  ``envoy.reloadable_features.experimental_matching_api`` to true.
* http: replaced setting ``envoy.reloadable_features.strict_1xx_and_204_response_headers`` with settings
  ``envoy.reloadable_features.require_strict_1xx_and_204_response_headers``
  (require upstream 1xx or 204 responses to not have Transfer-Encoding or non-zero Content-Length headers) and
  ``envoy.reloadable_features.send_strict_1xx_and_204_response_headers``
  (do not send 1xx or 204 responses with these headers). Both are true by default.
* http: serve HEAD requests from cache.
* http: the behavior of the *present_match* in route header matcher changed. The value of *present_match* is ignored in the past. The new behavior is *present_match* performed when value is true. absent match performed when the value is false. Please reference :ref:`present_match
  <envoy_v3_api_field_config.route.v3.HeaderMatcher.present_match>`.
* listener: respect the :ref:`connection balance config <envoy_v3_api_field_config.listener.v3.Listener.connection_balance_config>`
  defined within the listener where the sockets are redirected to. Clear that field to restore the previous behavior.
* tcp: switched to the new connection pool by default. Any unexpected behavioral changes can be reverted by setting runtime guard ``envoy.reloadable_features.new_tcp_connection_pool`` to false.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* aws_lambda: if `payload_passthrough` is set to ``false``, the downstream response content-type header will now be set from the content-type entry in the JSON response's headers map, if present.
* hot_restart: fix double counting of `server.seconds_until_first_ocsp_response_expiring` and `server.days_until_first_cert_expiring` during hot-restart. This stat was only incorrect until the parent process terminated.
* http: port stripping now works for CONNECT requests, though the port will be restored if the CONNECT request is sent upstream. This behavior can be temporarily reverted by setting ``envoy.reloadable_features.strip_port_from_connect`` to false.
* http: raise max configurable max_request_headers_kb limit to 8192 KiB (8MiB) from 96 KiB in http connection manager.
* listener: fix the crash which could happen when the ongoing filter chain only listener update is followed by the listener removal or full listener update.
* udp: limit each UDP listener to read maxmium 6000 packets per event loop. This behavior can be temporarily reverted by setting ``envoy.reloadable_features.udp_per_event_loop_read_limit`` to false.
* validation: fix an issue that causes TAP sockets to panic during config validation mode.
* xray: fix the default sampling 'rate' for AWS X-Ray tracer extension to be 5% as opposed to 50%.
* zipkin: fix timestamp serializaiton in annotations. A prior bug fix exposed an issue with timestamps being serialized as strings.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* event: removed ``envoy.reloadable_features.activate_timers_next_event_loop`` runtime guard and legacy code path.
* http: removed ``envoy.reloadable_features.allow_500_after_100`` runtime guard and the legacy code path.
* http: removed ``envoy.reloadable_features.always_apply_route_header_rules`` runtime guard and legacy code path.
* http: removed ``envoy.reloadable_features.hcm_stream_error_on_invalid_message`` for disabling closing HTTP/1.1 connections on error. Connection-closing can still be disabled by setting the HTTP/1 configuration :ref:`override_stream_error_on_invalid_http_message <envoy_v3_api_field_config.core.v3.Http1ProtocolOptions.override_stream_error_on_invalid_http_message>`.
* http: removed ``envoy.reloadable_features.http_set_copy_replace_all_headers`` runtime guard and legacy code paths.
* http: removed ``envoy.reloadable_features.overload_manager_disable_keepalive_drain_http2``; Envoy will now always send GOAWAY to HTTP2 downstreams when the :ref:`disable_keepalive <config_overload_manager_overload_actions>` overload action is active.
* http: removed ``envoy.reloadable_features.http_match_on_all_headers`` runtime guard and legacy code paths.
* http: removed ``envoy.reloadable_features.unify_grpc_handling`` runtime guard and legacy code paths.
* tls: removed ``envoy.reloadable_features.tls_use_io_handle_bio`` runtime guard and legacy code path.

New Features
------------

* bandwidth_limit: added new :ref:`HTTP bandwidth limit filter <config_http_filters_bandwidth_limit>`.
* crash support: restore crash context when continuing to processing requests or responses as a result of an asynchronous callback that invokes a filter directly. This is unlike the call stacks that go through the various network layers, to eventually reach the filter. For a concrete example see: ``Envoy::Extensions::HttpFilters::Cache::CacheFilter::getHeaders`` which posts a callback on the dispatcher that will invoke the filter directly.
* dynamic_forward_proxy: added :ref:`dns_resolver<envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_resolver>` option to the DNS cache config in order use custom DNS resolvers instead of the system default resolvers.
* http: a new field `is_optional` is added to `extensions.filters.network.http_connection_manager.v3.HttpFilter`. When
  value is `true`, the unsupported http filter will be ignored by envoy. This is also same with unsupported http filter
  in the typed per filter config. For more information, please reference
  :ref:`HttpFilter <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.is_optional>`.
* http: added :ref:`stripping trailing host dot from host header<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.strip_trailing_host_dot>` support.
* http: added support for :ref:`original IP detection extensions<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`.
  Two initial extensions were added, the :ref:`custom header <envoy_v3_api_msg_extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig>` extension and the
  :ref:`xff <envoy_v3_api_msg_extensions.http.original_ip_detection.xff.v3.XffConfig>` extension.
* http: added the ability to :ref:`unescape slash sequences<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.path_with_escaped_slashes_action>` in the path. Requests with unescaped slashes can be proxied, rejected or redirected to the new unescaped path. By default this feature is disabled. The default behavior can be overridden through :ref:`http_connection_manager.path_with_escaped_slashes_action<config_http_conn_man_runtime_path_with_escaped_slashes_action>` runtime variable. This action can be selectively enabled for a portion of requests by setting the :ref:`http_connection_manager.path_with_escaped_slashes_action_sampling<config_http_conn_man_runtime_path_with_escaped_slashes_action_enabled>` runtime variable.
* http: added upstream and downstream alpha HTTP/3 support! See :ref:`quic_options <envoy_v3_api_field_config.listener.v3.UdpListenerConfig.quic_options>` for downstream and the new http3_protocol_options in :ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` for upstream HTTP/3.
* listener: added ability to change an existing listener's address.
* local_rate_limit_filter: added suppoort for locally rate limiting http requests on a per connection basis. This can be enabled by setting the :ref:`local_rate_limit_per_downstream_connection <envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.local_rate_limit_per_downstream_connection>` field to true.
* metric service: added support for sending metric tags as labels. This can be enabled by setting the :ref:`emit_tags_as_labels <envoy_v3_api_field_config.metrics.v3.MetricsServiceConfig.emit_tags_as_labels>` field to true.
* tcp: added support for :ref:`preconnecting <v1.18.0:envoy_v3_api_msg_config.cluster.v3.Cluster.PreconnectPolicy>`. Preconnecting is off by default, but recommended for clusters serving latency-sensitive traffic.
* thrift_proxy: added per upstream metrics within the :ref:`thrift router <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.router.v3.Router>` for request and response size histograms.
* tls: allow dual ECDSA/RSA certs via SDS. Previously, SDS only supported a single certificate per context, and dual cert was only supported via non-SDS.
* udp_proxy: added :ref:`key <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.HashPolicy>` as another hash policy to support hash based routing on any given key.

Deprecated
----------

* http: :ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>` is deprecated in favor of :ref:`original IP detection extensions<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`.
