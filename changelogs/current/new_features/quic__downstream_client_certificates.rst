QUIC downstream listeners now support client certificate authentication (mutual TLS). When a filter
chain's :ref:`downstream TLS context
<envoy_v3_api_msg_extensions.transport_sockets.quic.v3.QuicDownstreamTransport>` sets
``require_client_certificate``, the server requests and validates the client certificate during the
handshake and exposes its fields to consumers such as ``x-forwarded-client-cert``, RBAC, and access
logs. The certificate is validated against the trust anchor of the filter chain matched for the
connection. Such a filter chain must also configure ``validation_context.trusted_ca`` and must not
set ``trust_chain_verification`` to ``ACCEPT_UNTRUSTED``. Without a trusted CA or with
``ACCEPT_UNTRUSTED`` the server would accept any client certificate.

This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.quic_mtls_server_enabled`` to ``false``, which restores the previous
behavior of rejecting QUIC listeners configured with ``require_client_certificate``.
