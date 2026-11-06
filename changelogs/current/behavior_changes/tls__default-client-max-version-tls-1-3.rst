Upstream TLS connections now default to a maximum TLS version of TLS 1.3 instead of TLS 1.2 when
:ref:`tls_maximum_protocol_version
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.TlsParameters.tls_maximum_protocol_version>`
is not set, which also lets them use the default post-quantum key exchange. When the upstream
server supports TLS 1.3, an mTLS client certificate rejected by the upstream is reported as a reset
after the connection is established rather than as a connection failure. In that case,
``connect-failure`` HTTP retries and TCP proxy
:ref:`max_connect_attempts
<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.max_connect_attempts>` don't
retry it on another endpoint, connect-only
:ref:`TCP health checks <envoy_v3_api_msg_config.core.v3.HealthCheck.TcpHealthCheck>` pass, and the
rejection isn't counted in the ``upstream_cx_connect_fail`` cluster statistic. Outlier detection
also records the connection as successful, so the host isn't ejected for consecutive local origin
failures caused by TCP proxy connections, or by HTTP requests when
:ref:`split_external_local_origin_errors
<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` is
enabled. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.default_client_tls_max_version_is_1_3`` to ``false``, or per cluster by
setting ``tls_maximum_protocol_version`` to ``TLSv1_2``.
