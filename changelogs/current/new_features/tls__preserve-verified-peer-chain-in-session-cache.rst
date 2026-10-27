Added experimental preservation of the complete originally validated peer certificate chain across
downstream TLS 1.2 stateful session-ID resumption with BoringSSL and the default handshaker.
Enable ``envoy.reloadable_features.preserve_verified_peer_chain_in_session_cache`` before creating
TLS contexts. The chain is attached to the internal cached session and follows its lifetime and
eviction. Verified issuer serial and digest accessors can use the restored chain;
``peerCertificateValidated()`` still describes validation on the current connection.
The feature is disabled by default and respects explicit disabling of stateful resumption.
