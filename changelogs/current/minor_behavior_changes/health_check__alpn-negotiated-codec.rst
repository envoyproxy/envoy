HTTP health checks now select their codec from the protocol negotiated by ALPN: a health check
connection that negotiates ``h2`` is checked over HTTP/2 and one that negotiates ``http/1.1`` over
HTTP/1.1. :ref:`codec_client_type
<envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.codec_client_type>` is used when
nothing is negotiated, i.e. for plaintext health checks, peers that do not do ALPN, and negotiated
protocols other than those two. What a health check connection advertises is unchanged: the
:ref:`tls_options <envoy_v3_api_msg_config.core.v3.HealthCheck.TlsOptions>` ``alpn_protocols`` if
set, otherwise the ``alpn_protocols`` of the cluster's TLS context. On a cluster pinned to a
protocol with ``explicit_http_config``, a health check connection that negotiates a different
protocol is now checked over the negotiated one, and can report healthy a host that data plane
traffic cannot reach; set ``tls_options`` ``alpn_protocols`` to the pinned protocol to keep such
checks pinned. ``codec_client_type: HTTP3`` is unchanged, and gRPC health checks are unaffected
since gRPC requires HTTP/2. This behavior can be reverted by setting runtime guard
``envoy.reloadable_features.health_check_use_negotiated_protocol`` to ``false``.
