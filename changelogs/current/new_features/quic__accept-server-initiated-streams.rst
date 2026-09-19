Added :ref:`accept_server_initiated_streams
<envoy_v3_api_field_config.core.v3.QuicProtocolOptions.accept_server_initiated_streams>`,
which lets an upstream HTTP/3 connection accept streams initiated by the server which belong to a
negotiated WebTransport session instead of rejecting them as a protocol violation. Only
bidirectional streams are accepted for now, and a server-initiated bidirectional stream which is
not a WebTransport data stream still closes the connection as a protocol violation. Defaults to
false, is ignored on listeners, and additionally requires the
``envoy.reloadable_features.quic_support_web_transport`` runtime feature.
