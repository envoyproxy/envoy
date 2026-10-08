Added ``AUTO`` to :ref:`codec_client_type
<envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.codec_client_type>`. An HTTP health
check configured with it selects its codec from the protocol negotiated by ALPN on the health check
connection: HTTP/2 when ``h2`` is negotiated, otherwise HTTP/1.1, the same as the cluster's
:ref:`auto_config <envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.auto_config>`.
The existing values keep using the configured protocol regardless of what is negotiated.
