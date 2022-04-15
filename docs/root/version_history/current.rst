1.22.0 (April 15, 2022)
=======================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* sip-proxy: change API by replacing ``own_domain`` with :ref:`local_services <envoy_v3_api_msg_extensions.filters.network.sip_proxy.v3alpha.LocalService>`.
* tls: set TLS v1.2 as the default minimal version for servers. Users can still explicitly opt-in to 1.0 and 1.1 using :ref:`tls_minimum_protocol_version <envoy_v3_api_field_extensions.transport_sockets.tls.v3.TlsParameters.tls_minimum_protocol_version>`.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* access_log: log all header values in the grpc access log.
* build: ``VERSION`` and ``API_VERSION`` have been renamed to ``VERSION.txt`` and ``API_VERSION.txt`` respectively to avoid conflicts with the C++ ``<version>`` header.
* config: type URL is used to lookup extensions regardless of the name field. This may cause problems for empty filter configurations or mis-matched protobuf as the typed configurations. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.no_extension_lookup_by_name`` to false.
* config: warning messages for protobuf unknown fields now contain ancestors for easier troubleshooting.
* cryptomb: remove RSA PKCS1 v1.5 padding support.
* decompressor: decompressor does not duplicate ``accept-encoding`` header values anymore. This behavioral change can be reverted by setting runtime guard ``envoy.reloadable_features.append_to_accept_content_encoding_only_once`` to false.
* dynamic_forward_proxy: if a DNS resolution fails, failing immediately with a specific resolution error, rather than finishing up all local filters and failing to select an upstream host.
* ecds: changed to use ``http_filter`` stat prefix as the metrics root for ECDS subscriptions. This behavior can be temporarily reverted by setting ``envoy.reloadable_features.top_level_ecds_stats`` to false.
* ext_authz: added requested server name in ext_authz network filter for auth review.
* ext_authz: forward :ref:`typed_filter_metadata <envoy_v3_api_field_config.core.v3.Metadata.typed_filter_metadata>` selected by :ref:`typed_metadata_context_namespaces <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.typed_metadata_context_namespaces>` to external auth service.
* file: changed disk based files to truncate files which are not being appended to. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.append_or_truncate`` to false.
* grpc: flip runtime guard ``envoy.reloadable_features.enable_grpc_async_client_cache`` to be default enabled. async grpc client created through ``getOrCreateRawAsyncClient`` will be cached by default.
* health_checker: exposing ``initial_metadata`` to GrpcHealthCheck in a way similar to ``request_headers_to_add`` of HttpHealthCheck.
* http: added the ability to have multiple URI type Subject Alternative Names of the client certificate in the :ref:`config_http_conn_man_headers_x-forwarded-client-cert` header.
* http: avoiding delay-close for HTTP/1.0 responses framed by ``connection: close`` as well as HTTP/1.1 if the request is fully read. This means for responses to such requests, the FIN will be sent immediately after the response. This behavior can be temporarily reverted by setting ``envoy.reloadable_features.skip_delay_close`` to false.  If clients are seen to be receiving sporadic partial responses and flipping this flag fixes it, please notify the project immediately.
* http: changed the http status code to 504 from 408 if the request times out after the request is completed. This behavior can be temporarily reverted by setting the runtime guard ``envoy.reloadable_features.override_request_timeout_by_gateway_timeout`` to false.
* http: lazy disable downstream connection reading in the HTTP/1 codec to reduce unnecessary system calls. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.http1_lazy_read_disable`` to false.
* http: now the max concurrent streams of http2 connection can not only be adjusted down according to the SETTINGS frame but also can be adjusted up. Of course, it can not exceed the configured upper bounds. This fix is guarded by ``envoy.reloadable_features.http2_allow_capacity_increase_by_settings``.
* http: respecting ``content-type`` in :ref:`headers_to_add <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.headers_to_add>` even when the response body is modified. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.allow_adding_content_type_in_local_replies`` to false.
* http: when writing custom filters, ``injectEncodedDataToFilterChain`` and ``injectDecodedDataToFilterChain`` now trigger sending of headers if they were not yet sent due to ``StopIteration``. Previously, calling one of the inject functions in that state would trigger an assertion. See issue #19891 for more details.
* listener: the :ref:`ipv4_compat <envoy_api_field_core.SocketAddress.ipv4_compat>` flag can only be set on Ipv6 address and Ipv4-mapped Ipv6 address. A runtime guard is added ``envoy.reloadable_features.strict_check_on_ipv4_compat`` and the default is true.
* network: add a new ConnectionEvent ``ConnectedZeroRtt`` which may be raised by QUIC connections to allow early data to be sent before the handshake finishes. This event is ignored at callsites which is only reachable for TCP connections in the Envoy core code. Any extensions which depend on ConnectionEvent enum value should audit their usage of it to make sure this new event is handled appropriately.
* oauth2: disable chunked transfer encoding in the token request to be compatible with Azure AD (login.microsoftonline.com).
* perf: tls contexts are now tracked without scan based garbage collection greatly improving the performance on secret update.
* ratelimit: the :ref:`header_value_match <envoy_v3_api_msg_config.route.v3.ratelimit.action.HeaderValueMatch>` config now supports custom descriptor keys.
* router: record upstream request timeouts for all cases and not just for those requests which are awaiting headers. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.do_not_await_headers_on_upstream_timeout_to_emit_stats`` to false.
* runtime: deprecated runtime flags set via configuration files or xDS will now ENVOY_BUG, rather than silently resulting in unexpected behavior on the data plane by no longer applying removed code paths.
* runtime: removed global runtime as Envoy default. This behavioral change can be reverted by setting runtime guard ``envoy.restart_features.no_runtime_singleton`` to false.
* sip-proxy: add customized affinity support by adding :ref:`tra_service_config <envoy_v3_api_msg_extensions.filters.network.sip_proxy.tra.v3alpha.TraServiceConfig>` and :ref:`customized_affinity <envoy_v3_api_msg_extensions.filters.network.sip_proxy.v3alpha.CustomizedAffinity>`.
* sip-proxy: add support for the ``503`` response code. When there is something wrong occurred, send ``503 Service Unavailable`` back to downstream.
* stateful session http filter: only enable cookie based session state when request path matches the configured cookie path.
* tracing: set tracing error tag for grpc non-ok response code only when it is a upstream error. Client error will not be tagged as a grpc error. This fix is guarded by ``envoy.reloadable_features.update_grpc_response_error_tag``.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* access_log: fix memory leak when reopening an access log fails. Access logs will now try to be reopened on each subsequent flush attempt after a failure.
* data plane: fix crash when internal redirect selects a route configured with direct response or redirect actions.
* data plane: fix error handling where writing to a socket failed while under the stack of processing. This should only effect HTTP/3. This behavioral change can be reverted by setting ``envoy.reloadable_features.allow_upstream_inline_write`` to false.
* eds: fix the eds cluster update by allowing update on the locality of the cluster endpoints. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.support_locality_update_on_eds_cluster_endpoints`` to false.
* hot restart: fixed a bug where an incorrect fd was passed to child when a tcp listener and a udp listener listen to the same address because socket type was not used to find the matching listener for a url.
* http: fixed a bug where ``%RESPONSE_CODE_DETAILS%`` was not set correctly in :ref:`request_headers_to_add <envoy_v3_api_field_config.route.v3.RouteConfiguration.request_headers_to_add>`.
* http: fixed a bug where ``100-continue`` comparison in the ``Expect`` request header field was case sensitive. This RFC compliant behavior can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.http_100_continue_case_insensitive`` to false.
* jwt_authn: fixed a bug where a JWT with empty "iss" is passed even the field :ref:`issuer <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.issuer>` is specified. If the "issuer" field is specified, "iss" in the JWT should match it.
* jwt_authn: fixed the crash when a CONNECT request is sent to JWT filter configured with regex match on the Host header.
* router: fixed mirror policy :ref:`runtime_fraction <envoy_v3_api_field_config.route.v3.RouteAction.RequestMirrorPolicy.runtime_fraction>` to
  correctly allow reading from a fractional percent value stored in runtime in all cases. Previously
  it would only do this if the default numerator was above 0, otherwise it would use the integer
  variant with a default of 0. The default of 0 is retained, but runtime lookup will happen in
  all cases and recognize a stored fractional percent.
* tcp_proxy: fix a crash that occurs when configured for :ref:`upstream tunneling <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.tunneling_config>` and the downstream connection disconnects while the the upstream connection or http/2 stream is still being established.
* tls: fix a bug while matching a certificate SAN with an exact value in ``match_typed_subject_alt_names`` of a listener where wildcard ``*`` character is not the only character of the dns label. Example, ``baz*.example.net`` and ``*baz.example.net`` and ``b*z.example.net`` will match ``baz1.example.net`` and ``foobaz.example.net`` and ``buzz.example.net``, respectively.
* upstream: added cluster slow start config :ref:`min_weight_percent <envoy_v3_api_field_config.cluster.v3.Cluster.SlowStartConfig.min_weight_percent>` field to avoid too big EDF deadline which cause slow start endpoints receiving no traffic, default 10%. This fix is related to `issue #19526 <https://github.com/envoyproxy/envoy/issues/19526>`_.
* upstream: fix stack overflow when a cluster with large number of idle connections is removed.
* xds: fix a crash that occurs when Envoy receives a discovery response without ``control_plane`` field.
* xds: fix the wildcard resource versions that are sent upon reconnection when using delta-xds mode.
* xray: fix the AWS X-Ray tracer extension to not sample the trace if ``sampled=`` keyword is not present in the header ``x-amzn-trace-id``.
* xray: fix the AWS X-Ray tracer extension to annotate a child span with ``type=subsegment`` to correctly relate subsegments to a parent segment. Previously a subsegment would be treated as an independent segment.
* xray: fix the AWS X-Ray tracer extension to reuse the trace ID already present in the header ``x-amzn-trace-id`` instead of creating a new one.
* xray: fix the AWS X-Ray tracer extension to set the HTTP ``X-Forwarded-For`` header value as ``client_ip`` in the segment data.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* access_log: removed ``envoy.reloadable_features.unquote_log_string_values`` and legacy code paths.
* grpc_bridge_filter: removed ``envoy.reloadable_features.grpc_bridge_stats_disabled`` and legacy code paths.
* http: removed ``envoy.reloadable_features.hash_multiple_header_values`` and legacy code paths.
* http: removed ``envoy.reloadable_features.no_chunked_encoding_header_for_304`` and legacy code paths.
* http: removed ``envoy.reloadable_features.preserve_downstream_scheme`` and legacy code paths.
* http: removed ``envoy.reloadable_features.require_strict_1xx_and_204_response_headers`` and ``envoy.reloadable_features.send_strict_1xx_and_204_response_headers`` and legacy code paths.
* http: removed ``envoy.reloadable_features.strip_port_from_connect`` and legacy code paths.
* http: removed ``envoy.reloadable_features.use_observable_cluster_name`` and legacy code paths.
* http: removed ``envoy.reloadable_features.http_transport_failure_reason_in_body`` and legacy code paths.
* http: removed ``envoy.reloadable_features.allow_response_for_timeout`` and legacy code paths.
* http: removed ``envoy.reloadable_features.http2_consume_stream_refused_errors`` and legacy code paths.
* http: removed ``envoy.reloadable_features.internal_redirects_with_body`` and legacy code paths.
* json: removed ``envoy.reloadable_features.remove_legacy_json`` and legacy code paths.
* listener: removed ``envoy.reloadable_features.listener_reuse_port_default_enabled`` and legacy code paths.
* listener: removed ``envoy.reloadable_features.listener_wildcard_match_ip_family`` and legacy code paths.
* udp: removed ``envoy.reloadable_features.udp_per_event_loop_read_limit`` and legacy code paths.
* upstream: removed ``envoy.reloadable_features.health_check.graceful_goaway_handling`` and legacy code paths.
* xds: removed ``envoy.reloadable_features.vhds_heartbeats`` and legacy code paths.


New Features
------------
* access_log: added new access_log command operator ``%GRPC_STATUS_NUMBER%``.
* access_log: added new access_log command operator ``%ENVIRONMENT(X):Z%``.
* access_log: added TCP proxy upstream and downstream byte logging. This can be accessed through the ``%DOWNSTREAM_WIRE_BYTES_SENT%``, ``%DOWNSTREAM_WIRE_BYTES_RECEIVED%``, ``%UPSTREAM_WIRE_BYTES_SENT%``, and ``%UPSTREAM_WIRE_BYTES_RECEIVED%`` access_log command operatrors.
* access_log: make consistent access_log format fields ``%(DOWN|DIRECT_DOWN|UP)STREAM_(LOCAL|REMOTE)_*%`` to provide all combinations of local & remote addresses for upstream & downstream connections.
* admin: :http:post:`/logging` now accepts ``/logging?paths=name1:level1,name2:level2,...`` to change multiple log levels at once.
* cluster: added support for per host limits in :ref:`circuit breakers settings <envoy_v3_api_msg_config.cluster.v3.CircuitBreakers>`. Currently only  :ref:`max_connections <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_connections>` is supported.
* cluster: added support to restore original destination address from any desired header via setting :ref:`http_header_name <envoy_v3_api_field_config.cluster.v3.Cluster.OriginalDstLbConfig.http_header_name>`.
* cluster: support :ref:`override host status restriction <envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.override_host_status>`.
* compression: add zstd :ref:`compressor <envoy_v3_api_msg_extensions.compression.zstd.compressor.v3.Zstd>` and :ref:`decompressor <envoy_v3_api_msg_extensions.compression.zstd.decompressor.v3.Zstd>`.
* config: added new file based xDS configuration via :ref:`path_config_source <envoy_v3_api_field_config.core.v3.ConfigSource.path_config_source>`.
  :ref:`watched_directory <envoy_v3_api_field_config.core.v3.PathConfigSource.watched_directory>` can
  be used to setup an independent watch for when to reload the file path, for example when using
  Kubernetes ConfigMaps to deliver configuration. See the linked documentation for more information.
* config: added new :ref:`custom config validators <config_config_validation>` to dynamically verify config updates.
* cors: add dynamic support for headers ``access-control-allow-methods`` and ``access-control-allow-headers`` in cors.
* dns: added :ref:`dns_min_refresh_rate <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_min_refresh_rate>`
  to the DNS cache implementation to configure the minimum DNS refresh rate, regardless of returned
  TTL. This was previously hard coded to 5s and defaults to 5s if unset.
* gcp authentication http filter: added :ref:`gcp authentication http filter <config_http_filters_gcp_authn>`.
* http: added ``random_value_specifier`` in :ref:`weighted_clusters <envoy_v3_api_field_config.route.v3.RouteAction.weighted_clusters>` to allow random value to be specified from configuration proto.
* http: added ``request_mirror_policies`` to higher levels (i.e., :ref:`request_mirror_policies <envoy_v3_api_field_config.route.v3.RouteConfiguration.request_mirror_policies>` in :ref:`RouteConfiguration <envoy_v3_api_msg_config.route.v3.RouteConfiguration>` and  :ref:`request_mirror_policies <envoy_v3_api_field_config.route.v3.VirtualHost.request_mirror_policies>` in :ref:`VirtualHost <envoy_v3_api_msg_config.route.v3.VirtualHost>`) which applies to :ref:`request_mirror_policies <envoy_v3_api_field_config.route.v3.RouteAction.request_mirror_policies>` in all routes underneath without configured mirror policies.
* http: added support for :ref:`cidr_ranges <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.InternalAddressConfig.cidr_ranges>` for configuring list of CIDR ranges that are considered internal.
* http: added support for :ref:`proxy_status_config <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.proxy_status_config>` for configuring `Proxy-Status <https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-proxy-status-08>`_ HTTP response header fields.
* http: make consistent custom header format fields ``%(DOWN|DIRECT_DOWN|UP)STREAM_(LOCAL|REMOTE)_*%`` to provide all combinations of local & remote addresses for upstream & downstream connections.
* http2: adds the new runtime feature ``envoy.reloadable_features.http2_use_oghttp2``, disabled by default, that guards use of a new HTTP/2 implementation.
* http2: re-enabled the HTTP/2 wrapper API. This should be a transparent change that does not affect functionality. Any behavior changes can be reverted by setting the ``envoy.reloadable_features.http2_new_codec_wrapper`` runtime feature to false.
* http3: add :ref:`enable_early_data <envoy_v3_api_field_extensions.transport_sockets.quic.v3.QuicDownstreamTransport.enable_early_data>` to turn on/off downstream early data support.
* http3: downstream HTTP/3 support is now GA! Upstream HTTP/3 also GA for specific deployments. See :ref:`here <arch_overview_http3>` for details.
* http3: supports upstream HTTP/3 retries. Automatically retry `0-RTT safe requests <https://www.rfc-editor.org/rfc/rfc7231#section-4.2.1>`_ if they are rejected because they are sent `too early <https://datatracker.ietf.org/doc/html/rfc8470#section-5.2>`_. And automatically retry 0-RTT safe requests if connect attempt fails later on and the cluster is configured with TCP fallback. And add retry on ``http3-post-connect-failure`` policy which allows retry of failed HTTP/3 requests with TCP fallback even after handshake if the cluster is configured with TCP fallback. This feature is guarded by ``envoy.reloadable_features.conn_pool_new_stream_with_early_data_and_http3``.
* listener: implement :ref:`matching API <arch_overview_matching_listener>` for selecting filter chains.
* local_ratelimit: added support for sharing the rate limiter between multiple network filter chains or listeners via :ref:`share_key <envoy_v3_api_field_extensions.filters.network.local_ratelimit.v3.LocalRateLimit.share_key>`.
* local_ratelimit: added support for ``X-RateLimit-*`` headers as defined in `draft RFC <https://tools.ietf.org/id/draft-polli-ratelimit-headers-03.html>`_.
* matching: the matching API can now express a match tree that will always match by omitting a matcher at the top level.
* outlier_detection: :ref:`max_ejection_time_jitter<envoy_v3_api_field_config.cluster.v3.OutlierDetection.base_ejection_time>` configuration added to allow adding a random value to the ejection time to prevent 'thundering herd' scenarios. Defaults to 0 so as to not break or change the behavior of existing deployments.
* ratelimit: added :ref:`rate_limited_status <envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.rate_limited_status>` to support return a custom HTTP response status code to the downstream client when the request has been rate limited.
* ratelimit: network rate limiter supports runtime value substitution using stream info and substitution formatting via :ref:`Network Rate Limiter <config_network_filters_ratelimit_substitution_formatter>`.
* redis: support for hostnames returned in ``cluster_slots`` response is now available.
* router: added :ref:`path_separated_prefix <envoy_v3_api_field_config.route.v3.RouteMatch.path_separated_prefix>` to make route creation more efficient.
* schema_validator_tool: added ``bootstrap`` checking to the
  :ref:`schema validator check tool <install_tools_schema_validator_check_tool>`.
* schema_validator_tool: added ``--fail-on-deprecated`` and ``--fail-on-wip`` to the
  :ref:`schema validator check tool <install_tools_schema_validator_check_tool>` to allow failing
  the check if either deprecated or work-in-progress fields are used.
* schema_validator_tool: fixed linking of all extensions into the
  :ref:`schema validator check tool <install_tools_schema_validator_check_tool>` so that all typed
  configurations can be properly verified.
* schema_validator_tool: the
  :ref:`schema validator check tool <install_tools_schema_validator_check_tool>` will now recurse
  into all sub messages, including Any messages, and perform full validation (deprecation,
  work-in-progress, PGV, etc.). Previously only top-level messages were fully validated.
* stats: histogram_buckets query parameter added to stats endpoint to change histogram output to show buckets.
* tap: added support for buffering an arbitrary number of tapped traces before returning to the client via a new :ref:`buffered admin sink <envoy_v3_api_field_config.tap.v3.OutputSink.buffered_admin>`.
* tcp_proxy: added support for on demand cluster. If the :ref:`on_demand <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.on_demand>` is set and the destination cluster is not present, a delta CDS request will be sent and the tcp proxy flow will be resumed after that cds response.
* thrift: add support for connection draining. This can be enabled by setting the runtime guard ``envoy.reloadable_features.thrift_connection_draining`` to true.
* thrift: added support for dynamic routing through aggregated discovery service.
* tls: add support for tls key log :ref:`key_log<envoy_v3_api_field_extensions.transport_sockets.tls.v3.CommonTlsContext.key_log>`.
* tools: the project now ships a :ref:`tools docker image <install_tools>` which contains tools
  useful in support systems such as CI, CD, etc. The
  :ref:`schema validator check tool <install_tools_schema_validator_check_tool>` has been added
  to the tools image.
* udp_proxy: added :ref:`matcher <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.matcher>` to support matching and routing to different clusters.
* udp_proxy: added support for :ref:`access_log <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.access_log>`.

Deprecated
----------

* config: deprecated :ref:`path <envoy_v3_api_field_config.core.v3.ConfigSource.path>` in favor of
  :ref:`path_config_source <envoy_v3_api_field_config.core.v3.ConfigSource.path_config_source>`
* http: deprecated ``envoy.http.headermap.lazy_map_min_size``.  If you are using this config knob you can revert this temporarily by setting ``envoy.reloadable_features.deprecate_global_ints`` to true but you MUST file an upstream issue to ensure this feature remains available.
* http: removing support for long-deprecated old style filter names, e.g. envoy.router, envoy.lua.
* re2: removed undocumented histograms ``re2.program_size`` and ``re2.exceeded_warn_level``.
* thrift: deprecated TTwitter protocol since we believe it's not used and it's causing significant maintenance burden.
* udp_proxy: deprecated :ref:`cluster <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.cluster>` in favor of :ref:`matcher <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.matcher>`.
