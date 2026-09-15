Added caching of parsed certificate material (CA certificate bundles, certificate revocation lists,
and certificate/key pairs) so that identical material is parsed once and reused across TLS contexts
that share the same input, instead of being re-parsed for every context. This is guarded by
``envoy.reloadable_features.cache_parsed_tls_certificates`` and defaults to false, so behavior is
unchanged unless the guard is explicitly enabled.
