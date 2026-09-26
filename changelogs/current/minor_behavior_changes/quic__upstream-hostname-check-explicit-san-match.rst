HTTP/3 upstream connections no longer require the server certificate to carry a DNS SAN matching the SNI when the
cluster's validation context configures an explicit identity check, i.e.
:ref:`match_typed_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_typed_subject_alt_names>`,
certificate hash or SPKI pins, a custom validator such as the SPIFFE validator, or a per-connection SAN list
override, and :ref:`auto_sni_san_validation
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.UpstreamTlsContext.auto_sni_san_validation>` is not set.
This matches the TLS client on TCP, where the configured identity check replaces the SNI-derived one, and allows
server certificates with only URI SANs such as X.509 SVIDs over HTTP/3. Validation contexts without an explicit
identity check keep the SNI check. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.quic_hostname_check_deferred_to_explicit_san_match`` to ``false``.
