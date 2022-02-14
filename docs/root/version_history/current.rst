1.22.0 (pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* tls: set TLS v1.2 as the default minimal version for servers. Users can still explicitly opt-in to 1.0 and 1.1 using :ref:`tls_minimum_protocol_version <envoy_v3_api_field_extensions.transport_sockets.tls.v3.TlsParameters.tls_minimum_protocol_version>`.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* dynamic_forward_proxy: if a DNS resolution fails, failing immediately with a specific resolution error, rather than finishing up all local filters and failing to select an upstream host.
* ext_authz: added requested server name in ext_authz network filter for auth review.
* file: changed disk based files to truncate files which are not being appended to. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.append_or_truncate`` to false.
* grpc: flip runtime guard ``envoy.reloadable_features.enable_grpc_async_client_cache`` to be default enabled. async grpc client created through getOrCreateRawAsyncClient will be cached by default.
* http: now the max concurrent streams of http2 connection can not only be adjusted down according to the SETTINGS frame but also can be adjusted up, of course, it can not exceed the configured upper bounds. This fix is guarded by ``envoy.reloadable_features.http2_allow_capacity_increase_by_settings``.
* http: when writing custom filters, `injectEncodedDataToFilterChain` and `injectDecodedDataToFilterChain` now trigger sending of headers if they were not yet sent due to `StopIteration`. Previously, calling one of the inject functions in that state would trigger an assertion. See issue #19891 for more details.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* data plane: fixing error handling where writing to a socket failed while under the stack of processing. This should genreally affect HTTP/3. This behavioral change can be reverted by setting ``envoy.reloadable_features.allow_upstream_inline_write`` to false.
* eds: fix the eds cluster update by allowing update on the locality of the cluster endpoints. This behavioral change can be temporarily reverted by setting runtime guard ``envoy.reloadable_features.support_locality_update_on_eds_cluster_endpoints`` to false.
* tls: fix a bug while matching a certificate SAN with an exact value in ``match_typed_subject_alt_names`` of a listener where wildcard ``*`` character is not the only character of the dns label. Example, ``baz*.example.net`` and ``*baz.example.net`` and ``b*z.example.net`` will match ``baz1.example.net`` and ``foobaz.example.net`` and ``buzz.example.net``, respectively.
* xray: fix the AWS X-Ray tracer extension to not sample the trace if ``sampled=`` keyword is not present in the header ``x-amzn-trace-id``.

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
* udp: removed ``envoy.reloadable_features.udp_per_event_loop_read_limit`` and legacy code paths.
* upstream: removed ``envoy.reloadable_features.health_check.graceful_goaway_handling`` and legacy code paths.

New Features
------------
* access_log: make consistent access_log format fields ``%(DOWN|DIRECT_DOWN|UP)STREAM_(LOCAL|REMOTE)_*%`` to provide all combinations of local & remote addresses for upstream & downstream connections.
* http: added random_value_specifier in :ref:`weighted_clusters <envoy_v3_api_field_config.route.v3.RouteAction.weighted_clusters>` to allow random value to be specified from configuration proto.
* http: added support for :ref:`proxy_status_config <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.proxy_status_config>` for configuring `Proxy-Status <https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-proxy-status-08>`_ HTTP response header fields.
* http: make consistent custom header format fields ``%(DOWN|DIRECT_DOWN|UP)STREAM_(LOCAL|REMOTE)_*%`` to provide all combinations of local & remote addresses for upstream & downstream connections.
* http3: downstream HTTP/3 support is now GA! Upstream HTTP/3 also GA for specific deployments. See :ref:`here <arch_overview_http3>` for details.
* matching: the matching API can now express a match tree that will always match by omitting a matcher at the top level.

Deprecated
----------

* http: removing support for long-deprecated old style filter names, e.g. envoy.router, envoy.lua.
