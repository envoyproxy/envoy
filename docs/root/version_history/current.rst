1.21.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* thrift_proxy: fix the thrift_proxy connection manager to correctly report success/error response metrics when performing :ref:`payload passthrough <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.ThriftProxy.payload_passthrough>`.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed ``envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure`` and legacy code paths.

New Features
------------
* access_log: added :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` token to handle all types of metadata (DYNAMIC, CLUSTER, ROUTE).
* bootstrap: added :ref:`inline_headers <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.inline_headers>` in the bootstrap to make custom inline headers bootstrap configurable.
* contrib: added new :ref:`contrib images <install_contrib>` which contain contrib extensions.
* dns: added :ref:`V4_PREFERRED <envoy_v3_api_enum_value_config.cluster.v3.Cluster.DnsLookupFamily.V4_PREFERRED>` option to return V6 addresses only if V4 addresses are not available.
* ext_authz: added :ref:`query_parameters_to_set <envoy_v3_api_field_service.auth.v3.OkHttpResponse.query_parameters_to_set>` and :ref:`query_parameters_to_remove <envoy_v3_api_field_service.auth.v3.OkHttpResponse.query_parameters_to_remove>` for adding and removing query string parameters when using a gRPC authorization server.
* ext_authz: added :ref:`dynamic_metadata_from_headers <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.AuthorizationResponse.dynamic_metadata_from_headers>` to support emitting dynamic metadata from headers returned by an external authorization service via HTTP.
* grpc reverse bridge: added a new :ref:`option <envoy_v3_api_field_extensions.filters.http.grpc_http1_reverse_bridge.v3.FilterConfig.response_size_header>` to support streaming response bodies when withholding gRPC frames from the upstream.
* grpc_json_transcoder: added support to unescape '+' in query parameters to space with a new config field :ref:`query_param_unescape_plus <envoy_v3_api_field_extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder.query_param_unescape_plus>`.
* http: added cluster_header in :ref:`weighted_clusters <envoy_v3_api_field_config.route.v3.RouteAction.weighted_clusters>` to allow routing to the weighted cluster specified in the request_header.
* http: added :ref:`alternate_protocols_cache_options <envoy_v3_api_msg_config.core.v3.AlternateProtocolsCacheOptions>` for enabling HTTP/3 connections to servers which advertise HTTP/3 support via `HTTP Alternative Services <https://tools.ietf.org/html/rfc7838>`_ and caching the advertisements to disk.
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
* rbac: added :ref:`matcher<envoy_v3_api_field_config.rbac.v3.Permission.matcher>` along with extension category ``extension_category_envoy.rbac.matchers`` for custom RBAC permission matchers. Added reference implementation for matchers :ref:`envoy.rbac.matchers.upstream_ip_port <extension_envoy.rbac.matchers.upstream_ip_port>`.
* route config: added :ref:`dynamic_metadata <envoy_v3_api_field_config.route.v3.RouteMatch.dynamic_metadata>` for routing based on dynamic metadata.
* router: added retry options predicate extensions configured via
  :ref:` <envoy_v3_api_field_config.route.v3.RetryPolicy.retry_options_predicates>`. These
  extensions allow modification of requests between retries at the router level. There are not
  currently any built-in extensions that implement this extension point.
* router: added :ref:`per_try_idle_timeout <envoy_v3_api_field_config.route.v3.RetryPolicy.per_try_idle_timeout>` timeout configuration.
* router: added an optional :ref:`override_auto_sni_header <envoy_v3_api_field_config.core.v3.UpstreamHttpProtocolOptions.override_auto_sni_header>` to support setting SNI value from an arbitrary header other than host/authority.
* sxg_filter: added filter to transform response to SXG package to :ref:`contrib images <install_contrib>`. This can be enabled by setting :ref:`SXG <envoy_v3_api_msg_extensions.filters.http.sxg.v3alpha.SXG>` configuration.
* thrift_proxy: added support for :ref:`mirroring requests <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.RouteAction.request_mirror_policies>`.
* udp: allows updating filter chain in-place through LDS, which is supported by Quic listener. Such listener config will be rejected in other connection-less UDP listener implementations. It can be reverted by ``envoy.reloadable_features.udp_listener_updates_filter_chain_in_place``.
* udp: disallow L4 filter chain in config which configures connection-less UDP listener. It can be reverted by ``envoy.reloadable_features.udp_listener_updates_filter_chain_in_place``.
* upstream: added support for :ref:`slow start mode <arch_overview_load_balancing_slow_start>`, which allows to progresively increase traffic for new endpoints.
* upstream: extended :ref:`Round Robin load balancer configuration <envoy_v3_api_field_config.cluster.v3.Cluster.round_robin_lb_config>` with :ref:`slow start <envoy_v3_api_field_config.cluster.v3.Cluster.RoundRobinLbConfig.slow_start_config>` support.
* upstream: extended :ref:`Least Request load balancer configuration <envoy_v3_api_field_config.cluster.v3.Cluster.least_request_lb_config>` with :ref:`slow start <envoy_v3_api_field_config.cluster.v3.Cluster.LeastRequestLbConfig.slow_start_config>` support.
* windows: added a new container image based on Windows Nanoserver 2022.
* xray: request direction (``ingress`` or ``egress``) is recorded as X-Ray trace segment's annotation by name ``direction``.

Deprecated
----------
