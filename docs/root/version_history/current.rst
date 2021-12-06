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
* dns: now respecting the returned DNS TTL for resolved hosts, rather than always relying on the hard-coded :ref:`dns_refresh_rate. <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` This behavior can be temporarily reverted by setting the runtime guard ``envoy.reloadable_features.use_dns_ttl`` to false.
* http: envoy will now proxy 102 and 103 headers from upstream, though as with 100s only the first 1xx response headers will be sent. This behavioral change by can temporarily reverted by setting runtime guard ``envoy.reloadable_features.proxy_102_103`` to false.
* http: usage of the experimental matching API is no longer guarded behind a feature flag, as the corresponding protobuf fields have been marked as WIP.
* http: when envoy run out of ``max_requests_per_connection``, it will send an HTTP/2 "shutdown nofitication" (GOAWAY frame with max stream ID) and go to a default grace period of 5000 milliseconds (5 seconds) if ``drain_timeout`` is not specified. During this grace period, envoy will continue to accept new streams. After the grace period, a final GOAWAY is sent and envoy will start refusing new streams. However before bugfix, during the grace period, every time a new stream is received, old envoy will always send a "shutdown notification" and restart drain again which actually causes the grace period to be extended and is no longer equal to ``drain_timeout``.
* json: switching from rapidjson to nlohmann/json. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.remove_legacy_json`` to false.
* listener: destroy per network filter chain stats when a network filter chain is removed during the listener in place update.
* quic: add back the support for IETF draft 29 which is guarded via ``envoy.reloadable_features.FLAGS_quic_reloadable_flag_quic_disable_version_draft_29``. It is off by default so Envoy only supports RFCv1 without flipping this runtime guard explicitly. Draft 29 is not recommended for use.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* ext_authz: fix the ext_authz http filter to correctly set response flags to ``UAEX`` when a connection is denied.
* ext_authz: fix the ext_authz network filter to correctly set response flag and code details to ``UAEX`` when a connection is denied.
* listener: fixed issue where more than one listener could listen on the same port if using reuse port, thus randomly accepting connections on different listeners. This configuration is now rejected.
* thrift_proxy: do not close downstream connections when an upstream connection overflow happens.
* thrift_proxy: fix the thrift_proxy connection manager to correctly report success/error response metrics when performing :ref:`payload passthrough <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.ThriftProxy.payload_passthrough>`.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* compression: removed ``envoy.reloadable_features.enable_compression_without_content_length_header`` runtime guard and legacy code paths.
* grpc-web: removed ``envoy.reloadable_features.grpc_web_fix_non_proto_encoded_response_handling`` and legacy code paths.
* header map: removed ``envoy.reloadable_features.header_map_correctly_coalesce_cookies`` and legacy code paths.
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
* http: removed ``envoy.reloadable_features.upstream_http2_flood_checks`` and legacy code paths.
* upstream: removed ``envoy.reloadable_features.upstream_host_weight_change_causes_rebuild`` and legacy code paths.

New Features
------------
* access log: added :ref:`grpc_stream_retry_policy <envoy_v3_api_field_extensions.access_loggers.grpc.v3.CommonGrpcAccessLogConfig.grpc_stream_retry_policy>` to the gRPC logger to reconnect when a connection fails to be established.
* access_log: added :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` token to handle all types of metadata (DYNAMIC, CLUSTER, ROUTE).
* access_log: added a CEL extension filter to enable filtering of access logs based on Envoy attribute expressions.
* access_log: added new access_log command operator ``%UPSTREAM_REQUEST_ATTEMPT_COUNT%`` to retrieve the number of times given request got attempted upstream.
* access_log: added new access_log command operator ``%VIRTUAL_CLUSTER_NAME%`` to retrieve the matched Virtual Cluster name.
* api: added support for *xds.type.v3.TypedStruct* in addition to the now-deprecated *udpa.type.v1.TypedStruct* proto message, which is a wrapper proto used to encode typed JSON data in a *google.protobuf.Any* field.
* aws_request_signing_filter: added :ref:`match_excluded_headers <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.match_excluded_headers>` to the signing filter to optionally exclude request headers from signing.
* bootstrap: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.typed_dns_resolver_config>` in the bootstrap to support DNS resolver as an extension.
* cluster: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.cluster.v3.Cluster.typed_dns_resolver_config>` in the cluster to support DNS resolver as an extension.
* config: added :ref:`environment_variable <envoy_v3_api_field_config.core.v3.datasource.environment_variable>` to the :ref:`DataSource <envoy_v3_api_msg_config.core.v3.datasource>`.
* dns: added :ref:`ALL <envoy_v3_api_enum_value_config.cluster.v3.Cluster.DnsLookupFamily.ALL>` option to return both IPv4 and IPv6 addresses.
* dns_cache: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.typed_dns_resolver_config>` in the dns_cache to support DNS resolver as an extension.
* dns_filter: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3.DnsFilterConfig.ClientContextConfig.typed_dns_resolver_config>` in the dns_filter to support DNS resolver as an extension.
* dns_resolver: added :ref:`CaresDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig>` to support c-ares DNS resolver as an extension.
* dns_resolver: added :ref:`AppleDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig>` to support apple DNS resolver as an extension.
* ext_authz: added :ref:`query_parameters_to_set <envoy_v3_api_field_service.auth.v3.OkHttpResponse.query_parameters_to_set>` and :ref:`query_parameters_to_remove <envoy_v3_api_field_service.auth.v3.OkHttpResponse.query_parameters_to_remove>` for adding and removing query string parameters when using a gRPC authorization server.
* http: added support for %REQUESTED_SERVER_NAME% to extract SNI as a custom header.
* http: added support for :ref:`retriable health check status codes <envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.retriable_statuses>`.
* http: added timing information about upstream connection and encryption establishment to stream info. These can currently be accessed via custom access loggers.
* http: added support for :ref:`forwarding HTTP1 reason phrase <envoy_v3_api_field_extensions.http.header_formatters.preserve_case.v3.PreserveCaseFormatterConfig.forward_reason_phrase>`.
* listener: added API for extensions to access :ref:`typed_filter_metadata <envoy_v3_api_field_config.core.v3.Metadata.typed_filter_metadata>` configured in the listener's :ref:`metadata <envoy_v3_api_field_config.listener.v3.Listener.metadata>` field.
* listener: added support for :ref:`MPTCP <envoy_v3_api_field_config.listener.v3.Listener.enable_mptcp>` (multipath TCP).
* listener: added support for opting out listeners from the globally set downstream connection limit via :ref:`ignore_global_conn_limit <envoy_v3_api_field_config.listener.v3.Listener.ignore_global_conn_limit>`.
* oauth filter: added :ref:`cookie_names <envoy_v3_api_field_extensions.filters.http.oauth2.v3.OAuth2Credentials.cookie_names>` to allow overriding (default) cookie names (``BearerToken``, ``OauthHMAC``, and ``OauthExpires``) set by the filter.
* oauth filter: setting IdToken and RefreshToken cookies if they are provided by Identity provider along with AccessToken.
* router: added support for the :ref:`config_http_conn_man_headers_x-forwarded-host` header.
* tcp: added a :ref:`FilterState <envoy_v3_api_msg_type.v3.HashPolicy.FilterState>` :ref:`hash policy <envoy_v3_api_msg_type.v3.HashPolicy>`, used by :ref:`TCP proxy <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.hash_policy>` to allow hashing load balancer algorithms to hash on objects in filter state.
* tcp_proxy: added support to populate upstream http connect header values from stream info.
* thrift_proxy: add header to metadata filter for turning headers into dynamic metadata.
* thrift_proxy: add upstream response zone metrics in the form ``cluster.cluster_name.zone.local_zone.upstream_zone.thrift.upstream_resp_success``.
* thrift_proxy: add upstream metrics to show decoding errors and whether exception is from local or remote, e.g. ``cluster.cluster_name.thrift.upstream_resp_exception_remote``.
* thrift_proxy: add host level success/error metrics where success is a reply of type success and error is any other response to a call.
* thrift_proxy: support header flags.
* thrift_proxy: support subset lb when using request or route metadata.
* tls: added support for :ref:`match_typed_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_typed_subject_alt_names>` for subject alternative names to enforce specifying the subject alternative name type for the matcher to prevent matching against an unintended type in the certificate.
* tls: added support for only verifying the leaf CRL in the certificate chain with :ref:`only_verify_leaf_cert_crl <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.only_verify_leaf_cert_crl>`.
* tls: support loading certificate chain and private key via :ref:`pkcs12 <envoy_v3_api_field_extensions.transport_sockets.tls.v3.TlsCertificate.pkcs12>`.
* tls_inspector filter: added :ref:`enable_ja3_fingerprinting <envoy_v3_api_field_extensions.filters.listener.tls_inspector.v3.TlsInspector.enable_ja3_fingerprinting>` to create JA3 fingerprint hash from Client Hello message.
* transport_socket: added :ref:`envoy.transport_sockets.tcp_stats <envoy_v3_api_msg_extensions.transport_sockets.tcp_stats.v3.Config>` which generates additional statistics gathered from the OS TCP stack.
* udp: add support for multiple listener filters.
* udp_proxy: added :ref:`use_per_packet_load_balancing <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.use_per_packet_load_balancing>` option to enable per packet load balancing (selection of upstream host on each data chunk).
* upstream: added the ability to :ref:`configure max connection duration <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_connection_duration>` for upstream clusters.
* vcl_socket_interface: added VCL socket interface extension for fd.io VPP integration to :ref:`contrib images <install_contrib>`. This can be enabled via :ref:`VCL <envoy_v3_api_msg_extensions.vcl.v3alpha.VclSocketInterface>` configuration.
* xds: re-introduced unified delta and sotw xDS multiplexers that share most of the implementation. Added a new runtime config ``envoy.reloadable_features.unified_mux`` (disabled by default) that when enabled, switches xDS to use unified multiplexers.

Deprecated
----------
* bootstrap: :ref:`dns_resolution_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.typed_dns_resolver_config>`.
* cluster: :ref:`dns_resolution_config <envoy_v3_api_field_config.cluster.v3.Cluster.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.cluster.v3.Cluster.typed_dns_resolver_config>`.
* dns_cache: :ref:`dns_resolution_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.typed_dns_resolver_config>`.
* tls: :ref:`match_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_subject_alt_names>` has been deprecated in favor of the :ref:`match_typed_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_typed_subject_alt_names>`.
* dns_filter: :ref:`dns_resolution_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3.DnsFilterConfig.ClientContextConfig.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3.DnsFilterConfig.ClientContextConfig.typed_dns_resolver_config>`.
