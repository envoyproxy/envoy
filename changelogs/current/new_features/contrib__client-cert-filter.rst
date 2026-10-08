Added a new :ref:`client_cert <config_http_filters_client_cert>` contrib HTTP filter
(``envoy.filters.http.client_cert``) that forwards the downstream mTLS client certificate to
upstreams using the ``Client-Cert`` and ``Client-Cert-Chain`` headers standardized by RFC 9440,
sanitizing any incoming occurrences of those headers.
