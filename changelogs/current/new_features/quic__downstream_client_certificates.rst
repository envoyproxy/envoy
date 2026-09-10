QUIC downstream listeners now support client certificate authentication (mutual TLS). When a
filter chain's :ref:`downstream TLS context
<envoy_v3_api_msg_extensions.transport_sockets.quic.v3.QuicDownstreamTransport>` sets
``require_client_certificate``, the server requires a client certificate during the handshake.
When the filter chain instead configures a certificate validation context without
``require_client_certificate``, the server requests but does not require one (optional mutual TLS),
and the handshake still succeeds if the client presents no certificate. Whenever the client
presents a certificate, it is validated against the trust anchor of the filter chain matched for the
connection and its fields are exposed to consumers such as ``x-forwarded-client-cert``, RBAC, and
access logs. A filter chain that requires a client certificate must also configure
``validation_context.trusted_ca`` and must not set ``trust_chain_verification`` to
``ACCEPT_UNTRUSTED``, since either would let the server accept any client certificate.

This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.quic_mtls_server_enabled`` to ``false``, which restores the previous
behavior of rejecting QUIC listeners that require a client certificate and not requesting a client
certificate for filter chains that configure an optional validation context.
