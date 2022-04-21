1.23.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* tls-inspector: the listener filter tls inspector's stats ``connection_closed`` and ``read_error`` are removed. The new stats are introduced for listener, ``downstream_peek_remote_close`` and ``read_error`` :ref:`listener stats <config_listener_stats>`.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* access_log: log all header values in the grpc access log.
* build: ``VERSION`` and ``API_VERSION`` have been renamed to ``VERSION.txt`` and ``API_VERSION.txt`` respectively to avoid conflicts with the C++ ``<version>`` header.
* config: warning messages for protobuf unknown fields now contain ancestors for easier troubleshooting.
* cryptomb: remove RSA PKCS1 v1.5 padding support.
* decompressor: decompressor does not duplicate ``accept-encoding`` header values anymore. This behavioral change can be reverted by setting runtime guard ``envoy.reloadable_features.append_to_accept_content_encoding_only_once`` to false.
* dynamic_forward_proxy: if a DNS resolution fails, failing immediately with a specific resolution error, rather than finishing up all local filters and failing to select an upstream host.
* ecds: changed to use ``http_filter`` stat prefix as the metrics root for ECDS subscriptions. This behavior can be temporarily reverted by setting ``envoy.reloadable_features.top_level_ecds_stats`` to false.
* ext_authz: added requested server name in ext_authz network filter for auth review.
* ext_authz: forward :ref:`typed_filter_metadata <envoy_v3_api_field_config.core.v3.Metadata.typed_filter_metadata>` selected by :ref:`typed_metadata_context_namespaces <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.typed_metadata_context_namespaces>` to external auth service.
* file: changed disk based files to truncate files which are not being appended to. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.append_or_truncate`` to false.
* grpc: flip runtime guard ``envoy.reloadable_features.enable_grpc_async_client_cache`` to be default enabled. async grpc client created through getOrCreateRawAsyncClient will be cached by default.
* health_checker: exposing ``initial_metadata`` to GrpcHealthCheck in a way similar to ``request_headers_to_add`` of HttpHealthCheck.
* http: avoiding delay-close for HTTP/1.0 responses framed by connection: close as well as HTTP/1.1 if the request is fully read. This means for responses to such requests, the FIN will be sent immediately after the response. This behavior can be temporarily reverted by setting ``envoy.reloadable_features.skip_delay_close`` to false.  If clients are are seen to be receiving sporadic partial responses and flipping this flag fixes it, please notify the project immediately.
* http: changed the http status code to 504 from 408 if the request timeouts after the request is completed. This behavior can be temporarily reverted by setting the runtime guard ``envoy.reloadable_features.override_request_timeout_by_gateway_timeout`` to false.
* http: lazy disable downstream connection reading in the HTTP/1 codec to reduce unnecessary system calls. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.http1_lazy_read_disable`` to false.
* http: now the max concurrent streams of http2 connection can not only be adjusted down according to the SETTINGS frame but also can be adjusted up, of course, it can not exceed the configured upper bounds. This fix is guarded by ``envoy.reloadable_features.http2_allow_capacity_increase_by_settings``.
* http: respecting ``content-type`` in :ref:`headers_to_add <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ResponseMapper.headers_to_add>` even when the response body is modified. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.allow_adding_content_type_in_local_replies`` to false.
* http: when writing custom filters, `injectEncodedDataToFilterChain` and `injectDecodedDataToFilterChain` now trigger sending of headers if they were not yet sent due to `StopIteration`. Previously, calling one of the inject functions in that state would trigger an assertion. See issue #19891 for more details.
* http: when writing custom filters, ``injectEncodedDataToFilterChain`` and ``injectDecodedDataToFilterChain`` now trigger sending of headers if they were not yet sent due to ``StopIteration``. Previously, calling one of the inject functions in that state would trigger an assertion. See issue #19891 for more details.
* listener: the :ref:`ipv4_compat <envoy_api_field_core.SocketAddress.ipv4_compat>` flag can only be set on Ipv6 address and Ipv4-mapped Ipv6 address. A runtime guard is added ``envoy.reloadable_features.strict_check_on_ipv4_compat`` and the default is true.
* network: add a new ConnectionEvent ``ConnectedZeroRtt`` which may be raised by QUIC connections to allow early data to be sent before the handshake finishes. This event is ignored at callsites which is only reachable for TCP connections in the Envoy core code. Any extensions which depend on ConnectionEvent enum value should audit their usage of it to make sure this new event is handled appropriately.
* perf: ssl contexts are now tracked without scan based garbage collection and greatly improved the performance on secret update.
* ratelimit: the :ref:`header_value_match <envoy_v3_api_msg_config.route.v3.ratelimit.action.HeaderValueMatch>` support custom descriptor_key.
* router: record upstream request timeouts for all the cases and not just for those requests which are awaiting headers. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.do_not_await_headers_on_upstream_timeout_to_emit_stats`` to false.
* runtime: deprecated runtime flags set via configuration files or xDS will now ENVOY_BUG, rather than silently resulting in unexpected behavior on the data plane by no longer applying removed code paths.
* runtime: removed global runtime as Envoy default. This behavioral change can be reverted by setting runtime guard ``envoy.restart_features.no_runtime_singleton`` to false.
* sip-proxy: add customized affinity support by adding :ref:`tra_service_config <envoy_v3_api_msg_extensions.filters.network.sip_proxy.tra.v3alpha.TraServiceConfig>` and :ref:`customized_affinity <envoy_v3_api_msg_extensions.filters.network.sip_proxy.v3alpha.CustomizedAffinity>`.
* sip-proxy: add support for the ``503`` response code. When there is something wrong occurred, send ``503 Service Unavailable`` back to downstream.
* stateful session http filter: only enable cookie based session state when request path matches the configured cookie path.
* tls: if both :ref:`match_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_subject_alt_names>` and :ref:`match_typed_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_typed_subject_alt_names>` are specified, the former (deprecated) field is ignored. Previously, setting both fields would result in an error.
* tls: removed SHA-1 cipher suites from the server-side defaults.
* tracing: set tracing error tag for grpc non-ok response code only when it is a upstream error. Client error will not be tagged as a grpc error. This fix is guarded by ``envoy.reloadable_features.update_grpc_response_error_tag``.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------
* dns_resolver: added :ref:`include_unroutable_families<envoy_v3_api_field_extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig.include_unroutable_families>` to the Apple DNS resolver.
* ext_proc: added support for per-route :ref:`grpc_service <envoy_v3_api_field_extensions.filters.http.ext_proc.v3.ExtProcOverrides.grpc_service>`.

Deprecated
----------
