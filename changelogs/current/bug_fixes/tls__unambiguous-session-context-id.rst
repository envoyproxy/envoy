Fixed TLS session resumption across server certificates or filter chains whose names differ but
concatenate to the same bytes, for example the DNS SANs ``a.example`` and ``b.example`` versus
``a.exampleb.example``. The session context ID that scopes resumption to a TLS context hashed the
common name and subject alternative names of each certificate, and each server name, without their
types or lengths. It now hashes the type and length of every field, which changes every session
context ID once, so a session established before the upgrade, or by an Envoy instance without this
change that shares the session ticket keys, takes a full handshake. This change can be temporarily
reverted by setting the runtime guard ``envoy.reloadable_features.unambiguous_tls_session_context_id``
to ``false``.
