
.. _config_http_filters_client_cert:

Client certificate forwarding (RFC 9440)
========================================

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.client_cert.v3alpha.ClientCertConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.client_cert.v3alpha.ClientCertConfig>`

.. attention::

   The client_cert filter is only included in :ref:`contrib images <install_contrib>`

.. attention::

   The client_cert filter is experimental and is currently under active development.

The client_cert filter forwards the downstream mutual TLS (mTLS) client certificate to upstream
services using the ``Client-Cert`` and ``Client-Cert-Chain`` HTTP header fields standardized by
`RFC 9440 <https://www.rfc-editor.org/rfc/rfc9440.html>`_. It is a standards-based alternative to
Envoy's :ref:`x-forwarded-client-cert <config_http_conn_man_headers_x-forwarded-client-cert>`
(XFCC) header for backends that consume the RFC 9440 format.

Each certificate is conveyed as an `RFC 8941 <https://datatracker.ietf.org/doc/html/rfc8941>`_
byte sequence: the base64 encoding of the DER certificate, delimited by colons (for example
``:MIIDsT...Vw==:``).

Injected headers
----------------

* ``Client-Cert``: the downstream client's end-entity (leaf) certificate. Set whenever the
  downstream connection is TLS with a client certificate presented.
* ``Client-Cert-Chain``: a comma-separated list of the certificates used to validate the client
  certificate, ordered from the certificate that issued the client certificate towards the trust
  anchor. Per RFC 9440 the end-entity certificate is not repeated in this header. The header is
  derived from the certificate chain Envoy actually built and verified during certificate
  validation, not from the raw chain presented by the peer, and is only emitted when
  :ref:`set_client_cert_chain
  <envoy_v3_api_field_extensions.filters.http.client_cert.v3alpha.ClientCertConfig.set_client_cert_chain>`
  is enabled and the validated chain is available.

.. note::

   Certificate chains can make the ``Client-Cert-Chain`` header large. Upstream servers may need
   to be configured to accept larger request headers, and the header is therefore disabled by
   default.

Security and sanitization
-------------------------

As required by RFC 9440 section 2.4, the filter defends against header spoofing:

* Any occurrence of ``Client-Cert`` or ``Client-Cert-Chain`` in the incoming request is always
  removed before forwarding, on every connection type.
* On cleartext connections, or TLS connections where the client did not present a certificate,
  no headers are injected, so the request reaches the upstream without either header.
* On mTLS connections, the headers are freshly generated from the authenticated downstream client
  certificate.

Example configuration
---------------------

Full filter configuration:

.. literalinclude:: _include/client_cert_filter.yaml
    :language: yaml
    :lines: 25-32
    :emphasize-lines: 2-5
    :linenos:
    :caption: :download:`client_cert_filter.yaml <_include/client_cert_filter.yaml>`
