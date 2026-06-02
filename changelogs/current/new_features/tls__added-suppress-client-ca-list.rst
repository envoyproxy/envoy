Added :ref:`suppress_client_ca_list
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.suppress_client_ca_list>`
option to CertificateValidationContext. When enabled, the server will not send the list of
trusted CA names to clients during the TLS handshake, while still using those CAs for
certificate validation. This avoids ``CertificateRequest`` messages exceeding TLS record limits
with very large CA sets, or when clients mishandle the CA set in some way. Honored by both
the built-in validator and the SPIFFE validator.
