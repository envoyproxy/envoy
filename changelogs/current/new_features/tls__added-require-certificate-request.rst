Added :ref:`require_certificate_request <envoy_v3_api_field_extensions.transport_sockets.tls.v3.UpstreamTlsContext.require_certificate_request>`
to ``UpstreamTlsContext``. When set, Envoy rejects upstream TLS connections where the server did not send a
``CertificateRequest``, ensuring mTLS is actually negotiated rather than silently downgraded to one-way TLS.
Rejections increment ``ssl.fail_no_cert_request`` and populate ``%UPSTREAM_TRANSPORT_FAILURE_REASON%``.
