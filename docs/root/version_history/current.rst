1.21.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* xds: ``*`` became a reserved name for a wildcard resource that can be subscribed to and unsubscribed from at any time. This is a requirement for implementing the on-demand xDSes (like on-demand CDS) that can subscribe to specific resources next to their wildcard subscription. If such xDS is subscribed to both wildcard resource and to other specific resource, then in stream reconnection scenario, the xDS will not send an empty initial request, but a request containing ``*`` for wildcard subscription and the rest of the resources the xDS is subscribed to. If the xDS is only subscribed to wildcard resource, it will try to send a legacy wildcard request. This behavior implements the recent changes in :ref:`xDS protocol <xds_protocol>` and can be temporarily reverted by setting the ``envoy.restart_features.explicit_wildcard_resource`` runtime guard to false.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* bandwidth_limit: added :ref:`response trailers <envoy_v3_api_field_extensions.filters.http.bandwidth_limit.v3.BandwidthLimit.enable_response_trailers>` when request or response delay are enforced.
* bandwidth_limit: added :ref:`bandwidth limit stats <config_http_filters_bandwidth_limit>` *request_enforced* and *response_enforced*.
* config: the log message for "gRPC config stream closed" now uses the most recent error message, and reports seconds instead of milliseconds for how long the most recent status has been received.
* dns: now respecting the returned DNS TTL for resolved hosts, rather than always relying on the hard-coded :ref:`dns_refresh_rate. <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` This behavior can be temporarily reverted by setting the runtime guard ``envoy.reloadable_features.use_dns_ttl`` to false.
* http: usage of the experimental matching API is no longer guarded behind a feature flag, as the corresponding protobuf fields have been marked as WIP.
* json: switching from rapidjson to nlohmann/json. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.remove_legacy_json`` to false.
* listener: destroy per network filter chain stats when a network filter chain is removed during the listener in place update.
* quic: add back the support for IETF draft 29 which is guarded via ``envoy.reloadable_features.FLAGS_quic_reloadable_flag_quic_disable_version_draft_29``. It is off by default so Envoy only supports RFCv1 without flipping this runtime guard explicitly. Draft 29 is not recommended for use.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* ext_authz: fix the ext_authz network filter to correctly set response flag and code details to ``UAEX`` when a connection is denied.
* listener: fixed the crash when updating listeners that do not bind to port.
* thrift_proxy: fix the thrift_proxy connection manager to correctly report success/error response metrics when performing :ref:`payload passthrough <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.ThriftProxy.payload_passthrough>`.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* compression: removed ``envoy.reloadable_features.enable_compression_without_content_length_header`` runtime guard and legacy code paths.
* grpc-web: removed ``envoy.reloadable_features.grpc_web_fix_non_proto_encoded_response_handling`` and legacy code paths.
* health check: removed ``envoy.reloadable_features.health_check.immediate_failure_exclude_from_cluster`` runtime guard and legacy code paths.
* http: removed ``envoy.reloadable_features.add_and_validate_scheme_header`` and legacy code paths.
* http: removed ``envoy.reloadable_features.check_unsupported_typed_per_filter_config``, Envoy will always check unsupported typed per filter config if the filter isn't optional.
* http: removed ``envoy.reloadable_features.dont_add_content_length_for_bodiless_requests deprecation`` and legacy code paths.
* http: removed ``envoy.reloadable_features.grpc_json_transcoder_adhere_to_buffer_limits`` and legacy code paths.
* http: removed ``envoy.reloadable_features.http2_skip_encoding_empty_trailers`` and legacy code paths. Envoy will always encode empty trailers by sending empty data with ``end_stream`` true (instead of sending empty trailers) for HTTP/2.
* http: removed ``envoy.reloadable_features.improved_stream_limit_handling`` and legacy code paths.
* http: removed ``envoy.reloadable_features.remove_forked_chromium_url`` and legacy code paths.
* http: removed ``envoy.reloadable_features.return_502_for_upstream_protocol_errors``. Envoy will always return 502 code upon encountering upstream protocol error.
* http: removed ``envoy.reloadable_features.treat_host_like_authority`` and legacy code paths.
* http: removed ``envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure`` and legacy code paths.
* upstream: removed ``envoy.reloadable_features.upstream_host_weight_change_causes_rebuild`` and legacy code paths.

New Features
------------
* access log: added :ref:`critical log message <envoy_v3_api_field_service.accesslog.v3.CriticalAccessLogsMessage.message>` to AccessLogService to guarantee log arrival.
* access_log: added :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` token to handle all types of metadata (DYNAMIC, CLUSTER, ROUTE).
* bootstrap: added :ref:`inline_headers <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.inline_headers>` in the bootstrap to make custom inline headers bootstrap configurable.
* contrib: added new :ref:`contrib images <install_contrib>` which contain contrib extensions.
* dns: added :ref:`V4_PREFERRED <envoy_v3_api_enum_value_config.cluster.v3.Cluster.DnsLookupFamily.V4_PREFERRED>` option to return V6 addresses only if V4 addresses are not available.
* grpc reverse bridge: added a new :ref:`option <envoy_v3_api_field_extensions.filters.http.grpc_http1_reverse_bridge.v3.FilterConfig.response_size_header>` to support streaming response bodies when withholding gRPC frames from the upstream.
* http: added cluster_header in :ref:`weighted_clusters <envoy_v3_api_field_config.route.v3.RouteAction.weighted_clusters>` to allow routing to the weighted cluster specified in the request_header.
* http: added :ref:`alternate_protocols_cache_options <envoy_v3_api_msg_config.core.v3.AlternateProtocolsCacheOptions>` for enabling HTTP/3 connections to servers which advertise HTTP/3 support via `HTTP Alternative Services <https://tools.ietf.org/html/rfc7838>`_.
* http: added :ref:`string_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.string_match>` in the header matcher.
* http: added :ref:`x-envoy-upstream-stream-duration-ms <config_http_filters_router_x-envoy-upstream-stream-duration-ms>` that allows configuring the max stream duration via a request header.
* http: added support for :ref:`max_requests_per_connection <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_requests_per_connection>` for both upstream and downstream connections.
* http: sanitizing the referer header as documented :ref:`here <config_http_conn_man_headers_referer>`. This feature can be temporarily turned off by setting runtime guard ``envoy.reloadable_features.sanitize_http_header_referer`` to false.
* http: validating outgoing HTTP/2 CONNECT requests to ensure that if ``:path`` is set that ``:protocol`` is present. This behavior can be temporarily turned off by setting runtime guard ``envoy.reloadable_features.validate_connect`` to false.
* jwt_authn: added support for :ref:`Jwt Cache <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.jwt_cache_config>` and its size can be specified by :ref:`jwt_cache_size <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtCacheConfig.jwt_cache_size>`.
* jwt_authn: added support for extracting JWTs from request cookies using :ref:`from_cookies <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.from_cookies>`.
* jwt_authn: added support for setting the extracted headers from a successfully verified JWT using :ref:`header_in_metadata <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.header_in_metadata>` to dynamic metadata.
* listener: new listener metric ``downstream_cx_transport_socket_connect_timeout`` to track transport socket timeouts.
* lua: added ``header:getAtIndex()`` and ``header:getNumValues()`` methods to :ref:`header object <config_http_filters_lua_header_wrapper>` for retrieving the value of a header at certain index and get the total number of values for a given header.
* matcher: added :ref:`invert <envoy_v3_api_field_type.matcher.v3.MetadataMatcher.invert>` for inverting the match result in the metadata matcher.
* overload: add a new overload action that resets streams using a lot of memory. To enable the tracking of allocated bytes in buffers that a stream is using we need to configure the minimum threshold for tracking via:ref:`buffer_factory_config <envoy_v3_api_field_config.overload.v3.OverloadManager.buffer_factory_config>`. We have an overload action ``Envoy::Server::OverloadActionNameValues::ResetStreams`` that takes advantage of the tracking to reset the most expensive stream first.
* rbac: added :ref:`destination_port_range <envoy_v3_api_field_config.rbac.v3.Permission.destination_port_range>` for matching range of destination ports.
* route config: added :ref:`dynamic_metadata <envoy_v3_api_field_config.route.v3.RouteMatch.dynamic_metadata>` for routing based on dynamic metadata.
* router: added retry options predicate extensions configured via
  :ref:` <envoy_v3_api_field_config.route.v3.RetryPolicy.retry_options_predicates>`. These
  extensions allow modification of requests between retries at the router level. There are not
  currently any built-in extensions that implement this extension point.
* router: added :ref:`per_try_idle_timeout <envoy_v3_api_field_config.route.v3.RetryPolicy.per_try_idle_timeout>` timeout configuration.
* router: added an optional :ref:`override_auto_sni_header <envoy_v3_api_field_config.core.v3.UpstreamHttpProtocolOptions.override_auto_sni_header>` to support setting SNI value from an arbitrary header other than host/authority.
* sxg_filter: added filter to transform response to SXG package to :ref:`contrib images <install_contrib>`. This can be enabled by setting :ref:`SXG <envoy_v3_api_msg_extensions.filters.http.sxg.v3alpha.SXG>` configuration.
* thrift_proxy: added support for :ref:`mirroring requests <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.RouteAction.request_mirror_policies>`.
* access log: added :ref:`grpc_stream_retry_policy <envoy_v3_api_field_extensions.access_loggers.grpc.v3.CommonGrpcAccessLogConfig.grpc_stream_retry_policy>` to the gRPC logger to reconnect when a connection fails to be established.
* api: added support for *xds.type.v3.TypedStruct* in addition to the now-deprecated *udpa.type.v1.TypedStruct* proto message, which is a wrapper proto used to encode typed JSON data in a *google.protobuf.Any* field.
* bootstrap: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.typed_dns_resolver_config>` in the bootstrap to support DNS resolver as an extension.
* cluster: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.cluster.v3.Cluster.typed_dns_resolver_config>` in the cluster to support DNS resolver as an extension.
* config: added :ref:`environment_variable <envoy_v3_api_field_config.core.v3.datasource.environment_variable>` to the :ref:`DataSource <envoy_v3_api_msg_config.core.v3.datasource>`.
* dns: added :ref:`ALL <envoy_v3_api_enum_value_config.cluster.v3.Cluster.DnsLookupFamily.ALL>` option to return both IPv4 and IPv6 addresses.
* dns_cache: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.typed_dns_resolver_config>` in the dns_cache to support DNS resolver as an extension.
* dns_filter: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3.DnsFilterConfig.ClientContextConfig.typed_dns_resolver_config>` in the dns_filter to support DNS resolver as an extension.
* dns_resolver: added :ref:`CaresDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig>` to support c-ares DNS resolver as an extension.
* dns_resolver: added :ref:`AppleDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig>` to support apple DNS resolver as an extension.
* ext_authz: added :ref:`query_parameters_to_set <envoy_v3_api_field_service.auth.v3.OkHttpResponse.query_parameters_to_set>` and :ref:`query_parameters_to_remove <envoy_v3_api_field_service.auth.v3.OkHttpResponse.query_parameters_to_remove>` for adding and removing query string parameters when using a gRPC authorization server.
* http: added support for :ref:`retriable health check status codes <envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.retriable_statuses>`.
* listener: added API for extensions to access :ref:`typed_filter_metadata <envoy_v3_api_field_config.core.v3.Metadata.typed_filter_metadata>` configured in the listener's :ref:`metadata <envoy_v3_api_field_config.listener.v3.Listener.metadata>` field.
* listener: added support for :ref:`MPTCP <envoy_v3_api_field_config.listener.v3.Listener.enable_mptcp>` (multipath TCP).
* oauth filter: added :ref:`cookie_names <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.cookie_names>` to allow overriding (default) cookie names (``BearerToken``, ``OauthHMAC``, and ``OauthExpires``) set by the filter.
* oauth filter: setting IdToken and RefreshToken cookies if they are provided by Identity provider along with AccessToken.
* tcp: added a :ref:`FilterState <envoy_v3_api_msg_type.v3.HashPolicy.FilterState>` :ref:`hash policy <envoy_v3_api_msg_type.v3.HashPolicy>`, used by :ref:`TCP proxy <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.hash_policy>` to allow hashing load balancer algorithms to hash on objects in filter state.
* tcp_proxy: added support to populate upstream http connect header values from stream info.
* thrift_proxy: add header to metadata filter for turning headers into dynamic metadata.
* thrift_proxy: add upstream response zone metrics in the form ``cluster.cluster_name.zone.local_zone.upstream_zone.thrift.upstream_resp_success``.
* thrift_proxy: add upstream metrics to show decoding errors and whether exception is from local or remote, e.g. ``cluster.cluster_name.thrift.upstream_resp_exception_remote``.
* thrift_proxy: add host level success/error metrics where success is a reply of type success and error is any other response to a call.
* thrift_proxy: support header flags.
* thrift_proxy: support subset lb when using request or route metadata.
* tls: added support for only verifying the leaf CRL in the certificate chain with :ref:`only_verify_leaf_cert_crl <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.only_verify_leaf_cert_crl>`.
* transport_socket: added :ref:`envoy.transport_sockets.tcp_stats <envoy_v3_api_msg_extensions.transport_sockets.tcp_stats.v3.Config>` which generates additional statistics gathered from the OS TCP stack.
* udp: add support for multiple listener filters.
* upstream: added the ability to :ref:`configure max connection duration <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_connection_duration>` for upstream clusters.
* vcl_socket_interface: added VCL socket interface extension for fd.io VPP integration to :ref:`contrib images <install_contrib>`. This can be enabled via :ref:`VCL <envoy_v3_api_msg_extensions.vcl.v3alpha.VclSocketInterface>` configuration.
* xds: re-introduced unified delta and sotw xDS multiplexers that share most of the implementation. Added a new runtime config ``envoy.reloadable_features.unified_mux`` (disabled by default) that when enabled, switches xDS to use unified multiplexers.

Deprecated
----------
* bootstrap: :ref:`dns_resolution_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.typed_dns_resolver_config>`.
* cluster: :ref:`dns_resolution_config <envoy_v3_api_field_config.cluster.v3.Cluster.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.cluster.v3.Cluster.typed_dns_resolver_config>`.
* dns_cache: :ref:`dns_resolution_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.typed_dns_resolver_config>`.
* dns_filter: :ref:`dns_resolution_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3.DnsFilterConfig.ClientContextConfig.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3.DnsFilterConfig.ClientContextConfig.typed_dns_resolver_config>`.
