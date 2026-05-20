Added support for TLS session ticket resumption in QUIC using configured session ticket keys from
:ref:`session_ticket_keys <envoy_v3_api_field_extensions.transport_sockets.tls.v3.DownstreamTlsContext.session_ticket_keys>`.
This enables faster reconnection across server instances by allowing clients to resume TLS sessions
without full handshakes. The feature is disabled by default and can be enabled by setting runtime guard
``envoy.reloadable_features.quic_session_ticket_support`` to ``true``.
