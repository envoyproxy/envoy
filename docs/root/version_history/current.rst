1.20.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

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
* http: set the default :ref:`lazy headermap threshold <arch_overview_http_header_map_settings>` to 3,
  which defines the minimal number of headers in a request/response/trailers required for using a
  dictionary in addition to the list. Setting the ``envoy.http.headermap.lazy_map_min_size`` runtime
  feature to a non-negative number will override the default value.
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

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* access log: fix ``%UPSTREAM_CLUSTER%`` when used in http upstream access logs. Previously, it was always logging as an unset value.
* access log: fix ``%UPSTREAM_CLUSTER%`` when used in http upstream access logs. Previously, it was always logging as an unset value.
* aws request signer: fix the AWS Request Signer extension to correctly normalize the path and query string to be signed according to AWS' guidelines, so that the hash on the server side matches. See `AWS SigV4 documentaion <https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html>`_.
* cluster: delete pools when they're idle to fix unbounded memory use when using PROXY protocol upstream with tcp_proxy. This behavior can be temporarily reverted by setting the ``envoy.reloadable_features.conn_pool_delete_when_idle`` runtime guard to false.
* hcm: remove deprecation for :ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>` and forbid mixing ip detection extensions with old related knobs.
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

New Features
------------
* access_log: added :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` token to handle all types of metadata (DYNAMIC, CLUSTER, ROUTE).
* bootstrap: added :ref:`inline_headers <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.inline_headers>` in the bootstrap to make custom inline headers bootstrap configurable.
* grpc reverse bridge: added a new :ref:`option <envoy_v3_api_field_extensions.filters.http.grpc_http1_reverse_bridge.v3.FilterConfig.response_size_header>` to support streaming response bodies when withholding gRPC frames from the upstream.
* http: added :ref:`string_match <envoy_v3_api_field_config.route.v3.HeaderMatcher.string_match>` in the header matcher.
* http: added support for :ref:`max_requests_per_connection <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_requests_per_connection>` for both upstream and downstream connections.
* jwt_authn: added support for :ref:`Jwt Cache <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.jwt_cache_config>` and its size can be specified by :ref:`jwt_cache_size <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtCacheConfig.jwt_cache_size>`.
* listener: new listener metric ``downstream_cx_transport_socket_connect_timeout`` to track transport socket timeouts.
* rbac: added :ref:`destination_port_range <envoy_v3_api_field_config.rbac.v3.Permission.destination_port_range>` for matching range of destination ports.
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
