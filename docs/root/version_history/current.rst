1.23.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* tls: removed SHA-1 cipher suites from the server-side defaults.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------
* access_log: added new access_log command operator ``%ENVIRONMENT(X):Z%``.
* access_log: added TCP proxy upstream and downstream byte logging. This can be accessed through the ``%DOWNSTREAM_WIRE_BYTES_SENT%``, ``%DOWNSTREAM_WIRE_BYTES_RECEIVED%``, ``%UPSTREAM_WIRE_BYTES_SENT%``, and ``%UPSTREAM_WIRE_BYTES_RECEIVED%`` access_log command operatrors.
* access_log: make consistent access_log format fields ``%(DOWN|DIRECT_DOWN|UP)STREAM_(LOCAL|REMOTE)_*%`` to provide all combinations of local & remote addresses for upstream & downstream connections.
* admin: :http:post:`/logging` now accepts ``/logging?paths=name1:level1,name2:level2,...`` to change multiple log levels at once.
* cluster: added support for per host limits in :ref:`circuit breakers settings <envoy_v3_api_msg_config.cluster.v3.CircuitBreakers>`. Currently only  :ref:`max_connections <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_connections>` is supported.
* cluster: added support to restore original destination address from any desired header via setting :ref:`http_header_name <envoy_v3_api_field_config.cluster.v3.Cluster.OriginalDstLbConfig.http_header_name>`.
* cluster: support :ref:`override host status restriction <envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.override_host_status>`.
* config: added new file based xDS configuration via :ref:`path_config_source <envoy_v3_api_field_config.core.v3.ConfigSource.path_config_source>`.
  :ref:`watched_directory <envoy_v3_api_field_config.core.v3.PathConfigSource.watched_directory>` can
  be used to setup an independent watch for when to reload the file path, for example when using
  Kubernetes ConfigMaps to deliver configuration. See the linked documentation for more information.
* config: added new :ref:`custom config validators <config_config_validation>` to dynamically verify config updates.
* cors: add dynamic support for headers ``access-control-allow-methods`` and ``access-control-allow-headers`` in cors.
* dns: added :ref:`dns_min_refresh_rate <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_min_refresh_rate>`
  to the DNS cache implementation to configure the minimum DNS refresh rate, regardless of returned
  TTL. This was previously hard coded to 5s and defaults to 5s if unset.
* ext_proc: added support for per-route :ref:`grpc_service <envoy_v3_api_field_extensions.filters.http.ext_proc.v3.ExtProcOverrides.grpc_service>`.
* http: added random_value_specifier in :ref:`weighted_clusters <envoy_v3_api_field_config.route.v3.RouteAction.weighted_clusters>` to allow random value to be specified from configuration proto.
* http: added request_mirror_policies to higher levels (i.e., :ref:`request_mirror_policies <envoy_v3_api_field_config.route.v3.RouteConfiguration.request_mirror_policies>` in :ref:`RouteConfiguration <envoy_v3_api_msg_config.route.v3.RouteConfiguration>` and  :ref:`request_mirror_policies <envoy_v3_api_field_config.route.v3.VirtualHost.request_mirror_policies>` in :ref:`VirtualHost <envoy_v3_api_msg_config.route.v3.VirtualHost>`) which applies to :ref:`request_mirror_policies <envoy_v3_api_field_config.route.v3.RouteAction.request_mirror_policies>` in all routes underneath without configured mirror policies.
* http: added support for :ref:`cidr_ranges <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.InternalAddressConfig.cidr_ranges>` for configuring list of CIDR ranges that are considered internal.
* http: added support for :ref:`proxy_status_config <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.proxy_status_config>` for configuring `Proxy-Status <https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-proxy-status-08>`_ HTTP response header fields.
* http: make consistent custom header format fields ``%(DOWN|DIRECT_DOWN|UP)STREAM_(LOCAL|REMOTE)_*%`` to provide all combinations of local & remote addresses for upstream & downstream connections.
* http2: adds the new runtime feature ``envoy.reloadable_features.http2_use_oghttp2``, disabled by default, that guards use of a new HTTP/2 implementation.
* http2: re-enabled the HTTP/2 wrapper API. This should be a transparent change that does not affect functionality. Any behavior changes can be reverted by setting the ``envoy.reloadable_features.http2_new_codec_wrapper`` runtime feature to false.
* http3: add :ref:`enable_early_data <envoy_v3_api_field_extensions.transport_sockets.quic.v3.QuicDownstreamTransport.enable_early_data>` to turn on/off downstream early data support.
* http3: downstream HTTP/3 support is now GA! Upstream HTTP/3 also GA for specific deployments. See :ref:`here <arch_overview_http3>` for details.
* http3: supports upstream HTTP/3 retries. Automatically retry `0-RTT safe requests <https://www.rfc-editor.org/rfc/rfc7231#section-4.2.1>`_ if they are rejected because they are sent `too early <https://datatracker.ietf.org/doc/html/rfc8470#section-5.2>`_. And automatically retry 0-RTT safe requests if connect attempt fails later on and the cluster is configured with TCP fallback. And add retry on ``http3-post-connect-failure`` policy which allows retry of failed HTTP/3 requests with TCP fallback even after handshake if the cluster is configured with TCP fallback. This feature is guarded by ``envoy.reloadable_features.conn_pool_new_stream_with_early_data_and_http3``.
* local_ratelimit: added support for sharing the rate limiter between multiple network filter chains or listeners via :ref:`share_key <envoy_v3_api_field_extensions.filters.network.local_ratelimit.v3.LocalRateLimit.share_key>`.
* local_ratelimit: added support for X-RateLimit-* headers as defined in `draft RFC <https://tools.ietf.org/id/draft-polli-ratelimit-headers-03.html>`_.
* matching: the matching API can now express a match tree that will always match by omitting a matcher at the top level.
* outlier_detection: :ref:`max_ejection_time_jitter<envoy_v3_api_field_config.cluster.v3.OutlierDetection.base_ejection_time>` configuration added to allow adding a random value to the ejection time to prevent 'thundering herd' scenarios. Defaults to 0 so as to not break or change the behavior of existing deployments.
* redis: support for hostnames returned in ``cluster_slots`` response is now available.
* router: added a path-separated prefix matcher, to make route creation more efficient. :ref:`path_separated_prefix <envoy_v3_api_field_config.route.v3.RouteMatch.path_separated_prefix>`.
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
