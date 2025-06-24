QUIC downstream listeners now support client certificate authentication (mutual TLS). When a filter
chain's :ref:`downstream TLS context
<envoy_v3_api_msg_extensions.transport_sockets.quic.v3.QuicDownstreamTransport>` sets
``require_client_certificate``, the server requests and validates the client certificate during the
handshake and exposes its fields to consumers such as ``x-forwarded-client-cert``, RBAC, and access
logs. Such a filter chain must also configure ``validation_context.trusted_ca`` and must not set
``trust_chain_verification`` to ``ACCEPT_UNTRUSTED``, either of which would accept any client
certificate. Listeners that select filter chains by SNI or source address should use a single trust
anchor across chains, because the certificate is validated against the default chain's trust anchor.
This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.quic_mtls_server_enabled`` to ``false``, which restores the previous
behavior of rejecting QUIC listeners configured with ``require_client_certificate``.
