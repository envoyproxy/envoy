Added ``%UPSTREAM_CLIENT_CERT_REQUESTED%`` access log formatter indicating whether the upstream sent a TLS
``CertificateRequest`` during the handshake. Returns ``true``, ``false``, or ``"-"`` (not tracked). Tracking
is active when :ref:`require_certificate_request <envoy_v3_api_field_extensions.transport_sockets.tls.v3.UpstreamTlsContext.require_certificate_request>`
is set or the ``envoy.reloadable_features.tls_upstream_record_cert_request`` runtime flag is enabled.
