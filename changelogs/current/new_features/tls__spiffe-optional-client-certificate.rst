Added opt-in support for optional downstream client certificates to the SPIFFE certificate validator
through :ref:`allow_optional_client_certificate
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig.allow_optional_client_certificate>`.
When enabled, :ref:`require_client_certificate
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.DownstreamTlsContext.require_client_certificate>`
controls whether a client certificate is required. Presented certificates are always validated.
By default, SPIFFE listeners continue to require a client certificate.
