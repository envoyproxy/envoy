Added :ref:`use_alpn_protocol
<envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.use_alpn_protocol>` to HTTP health
checks. With it set, the health check selects its codec from the protocol negotiated by ALPN on the
health check connection: HTTP/2 when ``h2`` is negotiated, HTTP/1.1 when ``http/1.1`` is
negotiated, and otherwise the configured ``codec_client_type``, HTTP/1.1 by default, which is the
same mapping as the cluster's :ref:`auto_config
<envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.auto_config>`. Like
``auto_config``, it is rejected on a cluster whose transport sockets do not support ALPN. Without
it, health checks keep using ``codec_client_type`` regardless of what is negotiated.
