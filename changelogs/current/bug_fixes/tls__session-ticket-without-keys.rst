Fixed a crash when a :ref:`custom TLS certificate selector
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.CommonTlsContext.custom_tls_certificate_selector>`
switched a handshake from a TLS context with session ticket keys to one without them, for example
a context that the selector built before the keys arrived over SDS. Such a handshake now completes
without a session ticket. The selectors that shipped with earlier Envoy releases were not affected,
since they only switched to separately built TLS contexts when session resumption was disabled.
