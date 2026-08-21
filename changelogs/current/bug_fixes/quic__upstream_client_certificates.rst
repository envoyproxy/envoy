Fixed upstream QUIC connections to present the client certificate configured in the cluster's
:ref:`upstream TLS context <envoy_v3_api_msg_extensions.transport_sockets.quic.v3.QuicUpstreamTransport>`
when the upstream server requests one. Previously configured client certificates were silently
not sent over HTTP/3. Client certificates using a private key provider are not supported over
QUIC and are skipped with a warning. This behavior change can be reverted by setting the runtime
guard ``envoy.reloadable_features.quic_upstream_client_certificates`` to ``false``.
