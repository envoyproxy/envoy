Added :ref:`certificate_compression
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.CommonTlsContext.certificate_compression>`
to choose which RFC 8879 certificate compression algorithms a TLS or QUIC listener or cluster
uses. Unset fields keep the current defaults.
The ``envoy.reloadable_features.tls_certificate_compression_brotli`` runtime guard now only sets
the default for unset fields, and will be removed in a future release.
