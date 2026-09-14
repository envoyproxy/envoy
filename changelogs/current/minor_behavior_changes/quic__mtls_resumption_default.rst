QUIC downstream filter chains that set ``require_client_certificate`` now default
:ref:`enable_resumption <envoy_v3_api_field_extensions.transport_sockets.quic.v3.QuicDownstreamTransport.enable_resumption>`
and :ref:`enable_early_data <envoy_v3_api_field_extensions.transport_sockets.quic.v3.QuicDownstreamTransport.enable_early_data>`
to false, because QUIC does not re-validate the client certificate when a session is resumed.
Resumption can still be enabled explicitly on such filter chains. This behavior can be reverted by
setting the runtime guard ``envoy.reloadable_features.quic_mtls_resumption_disabled_by_default``
to ``false``.
