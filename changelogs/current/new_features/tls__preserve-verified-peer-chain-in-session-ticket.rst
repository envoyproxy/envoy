Added an experimental opt-in ticket envelope that preserves the complete originally validated
peer certificate chain across downstream TLS 1.2 and TLS 1.3 ticket resumption with BoringSSL.
Enable ``envoy.reloadable_features.preserve_verified_peer_chain_in_session_ticket`` before
creating TLS contexts. Verified issuer serial and digest accessors can use the restored chain;
``peerCertificateValidated()`` continues to describe validation on the current connection.
Stateful session-ID caching is not covered. Changing the feature setting changes the ticket
format and causes incompatible tickets to fall back to a full handshake.
