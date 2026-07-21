.. _config_http_filters_rfc9440_client_cert:

RFC 9440 Client-Cert Filter
===========================

The RFC 9440 Client-Cert filter evaluates downstream mutual TLS (mTLS) connections and forwards 
the client's certificate chain to upstream services using standard, standardized client certificate forwarding headers.

The injected headers adhere strictly to the `RFC 9440 (Client-Cert HTTP Header Field) <https://datatracker.ietf.org/doc/html/rfc9440>`_ 
and are encoded as `RFC 8941 Structured Field <https://datatracker.ietf.org/doc/html/rfc8941>`_ byte sequences (base64 enclosed in colons).

Security and Sanitization
-------------------------

To defend against header-spoofing attacks, the filter enforces the following constraints:

* **Cleartext or Unauthenticated Connections:** If the request arrives over a cleartext connection or a TLS connection where the client did not present a certificate, any incoming ``Client-Cert`` or ``Client-Cert-Chain`` headers are completely stripped before the request is forwarded.
* **Authenticated mTLS Connections:** If valid client certificates are present, any incoming spoofed ``Client-Cert`` and ``Client-Cert-Chain`` headers are wiped out, and newly generated values derived from the authenticated downstream client certificate.

Injected Headers
----------------

* ``Client-Cert``: Contains the base64-encoded ASN.1 DER-representation of the leaf client certificate, formatted as a structured field byte sequence (e.g., ``:YmFzZTY0...:``).
* ``Client-Cert-Chain``: A comma-separated list containing the intermediate certificates from the client certificate chain, formatted as structured field byte sequences. The leaf certificate is omitted because it is represented separately by ``Client-Cert``.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.rfc9440_client_cert.v3.Rfc9440ClientCert``.
