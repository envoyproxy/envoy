Added :ref:`use_alpn_negotiated_protocol
<envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.use_alpn_negotiated_protocol>` to
HTTP health checks. With it set, the health check selects its codec from the protocol negotiated by
ALPN on the health check connection: HTTP/2 when ``h2`` is negotiated, otherwise HTTP/1.1, the same
as the cluster's :ref:`auto_config
<envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.auto_config>`. Like
``auto_config``, it is rejected on a cluster whose transport sockets do not support ALPN. Without
it, health checks keep using the configured ``codec_client_type`` regardless of what is negotiated.
