1.20.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* config: the ``--bootstrap-version`` CLI flag has been removed, Envoy has only been able to accept v3
  bootstrap configurations since 1.18.0.
* contrib: the :ref:`squash filter <config_http_filters_squash>` has been moved to
  :ref:`contrib images <install_contrib>`.
* contrib: the :ref:`kafka broker filter <config_network_filters_kafka_broker>` has been moved to
  :ref:`contrib images <install_contrib>`.
* contrib: the :ref:`RocketMQ proxy filter <config_network_filters_rocketmq_proxy>` has been moved to
  :ref:`contrib images <install_contrib>`.
* contrib: the :ref:`Postgres proxy filter <config_network_filters_postgres_proxy>` has been moved to
  :ref:`contrib images <install_contrib>`.
* contrib: the :ref:`MySQL proxy filter <config_network_filters_mysql_proxy>` has been moved to
  :ref:`contrib images <install_contrib>`.
* ext_authz: fixed skipping authentication when returning either a direct response or a redirect. This behavior can be temporarily reverted by setting the ``envoy.reloadable_features.http_ext_authz_do_not_skip_direct_response_and_redirect`` runtime guard to false.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* config: configuration files ending in .yml now load as YAML.
* config: configuration file extensions now ignore case when deciding the file type. E.g., .JSON file load as JSON.
* config: reduced log level for "Unable to establish new stream" xDS logs to debug. The log level
  for "gRPC config stream closed" is now reduced to debug when the status is ``Ok`` or has been
  retriable (``DeadlineExceeded``, ``ResourceExhausted``, or ``Unavailable``) for less than 30
  seconds.
* grpc: gRPC async client can be cached and shared accross filter instances in the same thread, this feature is turned off by default, can be turned on by setting runtime guard ``envoy.reloadable_features.enable_grpc_async_client_cache`` to true.
* http: correct the use of the ``x-forwarded-proto`` header and the ``:scheme`` header. Where they differ
  (which is rare) ``:scheme`` will now be used for serving redirect URIs and cached content. This behavior
  can be reverted by setting runtime guard ``correct_scheme_and_xfp`` to false.
* http: reject requests with #fragment in the URI path. The fragment is not allowed to be part of request
  URI according to RFC3986 (3.5), RFC7230 (5.1) and RFC 7540 (8.1.2.3). Rejection of requests can be changed
  to stripping the #fragment instead by setting the runtime guard ``envoy.reloadable_features.http_reject_path_with_fragment``
  to false. This behavior can further be changed to the deprecated behavior of keeping the fragment by setting the runtime guard
  ``envoy.reloadable_features.http_strip_fragment_from_path_unsafe_if_disabled``. This runtime guard must only be set
  to false when existing non-compliant traffic relies on #fragment in URI. When this option is enabled, Envoy request
  authorization extensions may be bypassed. This override and its associated behavior will be decommissioned after the standard deprecation period.
* http: set the default :ref:`lazy headermap threshold <arch_overview_http_header_map_settings>` to 3,
  which defines the minimal number of headers in a request/response/trailers required for using a
  dictionary in addition to the list. Setting the ``envoy.http.headermap.lazy_map_min_size`` runtime
  feature to a non-negative number will override the default value.
* http: stop processing pending H/2 frames if connection transitioned to a closed state. This behavior can be temporarily reverted by setting the ``envoy.reloadable_features.skip_dispatching_frames_for_closed_connection`` to false.
* listener: added the :ref:`enable_reuse_port <envoy_v3_api_field_config.listener.v3.Listener.enable_reuse_port>`
  field and changed the default for reuse_port from false to true, as the feature is now well
  supported on the majority of production Linux kernels in use. The default change is aware of hot
  restart, as otherwise the change would not be backwards compatible between restarts. This means
  that hot restarting on to a new binary will retain the default of false until the binary undergoes
  a full restart. To retain the previous behavior, either explicitly set the new configuration
  field to false, or set the runtime feature flag ``envoy.reloadable_features.listener_reuse_port_default_enabled``
  to false. As part of this change, the use of reuse_port for TCP listeners on both macOS and
  Windows has been disabled due to suboptimal behavior. See the field documentation for more
  information.
* listener: destroy per network filter chain stats when a network filter chain is removed during the listener in place update.
* quic: enables IETF connection migration. This feature requires stable UDP packet routine in the L4 load balancer with the same first-4-bytes in connection id. It can be turned off by setting runtime guard ``envoy.reloadable_features.FLAGS_quic_reloadable_flag_quic_connection_migration_use_new_cid_v2`` to false.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* access log: fix ``%UPSTREAM_CLUSTER%`` when used in http upstream access logs. Previously, it was always logging as an unset value.
* access log: fix ``%UPSTREAM_CLUSTER%`` when used in http upstream access logs. Previously, it was always logging as an unset value.
* aws request signer: fix the AWS Request Signer extension to correctly normalize the path and query string to be signed according to AWS' guidelines, so that the hash on the server side matches. See `AWS SigV4 documentaion <https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html>`_.
* cluster: delete pools when they're idle to fix unbounded memory use when using PROXY protocol upstream with tcp_proxy. This behavior can be temporarily reverted by setting the ``envoy.reloadable_features.conn_pool_delete_when_idle`` runtime guard to false.
* cluster: finish cluster warming even if hosts are removed before health check initialization. This only affected clusters with :ref:`ignore_health_on_host_removal <envoy_v3_api_field_config.cluster.v3.Cluster.ignore_health_on_host_removal>`.
* dynamic forward proxy: fixing a validation bug where san and sni checks were not applied setting :ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>` via :ref:`typed_extension_protocol_options <envoy_v3_api_field_config.cluster.v3.Cluster.typed_extension_protocol_options>`.
* ext_authz: fix the ext_authz filter to correctly merge multiple same headers using the ',' as separator in the check request to the external authorization service.
* ext_authz: the network ext_authz filter now correctly sets dynamic metdata returned by the authorization service for non-OK responses. This behavior now matches the http ext_authz filter.
* hcm: remove deprecation for :ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>` and forbid mixing ip detection extensions with old related knobs.
* http: limit use of deferred resets in the http2 codec to server-side connections. Use of deferred reset for client connections can result in incorrect behavior and performance problems.
* listener: fixed an issue on Windows where connections are not handled by all worker threads.
* xray: fix the AWS X-Ray tracer bug where span's error, fault and throttle information was not reported properly as per the `AWS X-Ray documentation <https://docs.aws.amazon.com/xray/latest/devguide/xray-api-segmentdocuments.html>`_. Before this fix, server error was reported under 'annotations' section of the segment data.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed ``envoy.reloadable_features.http_upstream_wait_connect_response`` runtime guard and legacy code paths.
* http: removed ``envoy.reloadable_features.allow_preconnect`` runtime guard and legacy code paths.
* listener: removed ``envoy.reloadable_features.disable_tls_inspector_injection`` runtime guard and legacy code paths.
* ocsp: removed ``envoy.reloadable_features.check_ocsp_policy deprecation`` runtime guard and legacy code paths.
* ocsp: removed ``envoy.reloadable_features.require_ocsp_response_for_must_staple_certs deprecation`` and legacy code paths.
* quic: removed ``envoy.reloadable_features.prefer_quic_kernel_bpf_packet_routing`` runtime guard.

New Features
------------
* access_log: added :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` token to handle all types of metadata (DYNAMIC, CLUSTER, ROUTE).
* bootstrap: added :ref:`inline_headers <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.inline_headers>` in the bootstrap to make custom inline headers bootstrap configurable.
* contrib: added new :ref:`contrib images <install_contrib>` which contain contrib extensions.
* grpc reverse bridge: added a new :ref:`option <envoy_v3_api_field_extensions.filters.http.grpc_http1_reverse_bridge.v3.FilterConfig.response_size_header>` to support streaming response bodies when withholding gRPC frames from the upstream.
* http: added :ref:`string_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.string_match>` in the header matcher.
* http: added :ref:`x-envoy-upstream-stream-duration-ms <config_http_filters_router_x-envoy-upstream-stream-duration-ms>` that allows configuring the max stream duration via a request header.
* http: added support for :ref:`max_requests_per_connection <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_requests_per_connection>` for both upstream and downstream connections.
* http: sanitizing the referer header as documented :ref:`here <config_http_conn_man_headers_referer>`. This feature can be temporarily turned off by setting runtime guard ``envoy.reloadable_features.sanitize_http_header_referer`` to false.
* jwt_authn: added support for :ref:`Jwt Cache <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.jwt_cache_config>` and its size can be specified by :ref:`jwt_cache_size <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtCacheConfig.jwt_cache_size>`.
* jwt_authn: added support for extracting JWTs from request cookies using :ref:`from_cookies <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.from_cookies>`.
* listener: new listener metric ``downstream_cx_transport_socket_connect_timeout`` to track transport socket timeouts.
* matcher: added :ref:`invert <envoy_v3_api_field_type.matcher.v3.MetadataMatcher.invert>` for inverting the match result in the metadata matcher.
* overload: add a new overload action that resets streams using a lot of memory. To enable the tracking of allocated bytes in buffers that a stream is using  we need to configure the minimum threshold for tracking via:ref:`buffer_factory_config <envoy_v3_api_field_config.overload.v3.OverloadManager.buffer_factory_config>`. We have an overload action ``Envoy::Server::OverloadActionNameValues::ResetStreams`` that takes advantage of the tracking to  reset the most expensive stream first.
* rbac: added :ref:`destination_port_range <envoy_v3_api_field_config.rbac.v3.Permission.destination_port_range>` for matching range of destination ports.
* route config: added :ref:`dynamic_metadata <envoy_v3_api_field_config.route.v3.RouteMatch.dynamic_metadata>` for routing based on dynamic metadata.
* sxg_filter: added filter to transform response to SXG package to :ref:`contrib images <install_contrib>`. This can be enabled by setting :ref:`SXG <envoy_v3_api_msg_extensions.filters.http.sxg.v3alpha.SXG>` configuration.
* thrift_proxy: added support for :ref:`mirroring requests <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.RouteAction.request_mirror_policies>`.

Deprecated
----------

* api: the :ref:`matcher <envoy_v3_api_field_extensions.common.matching.v3.ExtensionWithMatcher.matcher>` field has been deprecated in favor of
  :ref:`matcher <envoy_v3_api_field_extensions.common.matching.v3.ExtensionWithMatcher.xds_matcher>` in order to break a build dependency.
* cluster: :ref:`max_requests_per_connection <envoy_v3_api_field_config.cluster.v3.Cluster.max_requests_per_connection>` is deprecated in favor of :ref:`max_requests_per_connection <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_requests_per_connection>`.
* http: the HeaderMatcher fields :ref:`exact_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.exact_match>`, :ref:`safe_regex_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.safe_regex_match>`,
  :ref:`prefix_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.prefix_match>`, :ref:`suffix_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.suffix_match>` and
  :ref:`contains_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.contains_match>` are deprecated by :ref:`string_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.string_match>`.
* listener: :ref:`reuse_port <envoy_v3_api_field_config.listener.v3.Listener.reuse_port>` has been
  deprecated in favor of :ref:`enable_reuse_port <envoy_v3_api_field_config.listener.v3.Listener.enable_reuse_port>`.
  At the same time, the default has been changed from false to true. See above for more information.
