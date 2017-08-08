.. _config_cluster_manager_cluster_ssl:

TLS context
===========

.. code-block:: json

  {
    "alpn_protocols": "...",
    "cert_chain_file": "...",
    "private_key_file": "...",
    "ca_cert_file": "...",
    "verify_certificate_hash": "...",
    "verify_subject_alt_name": [],
    "cipher_suites": "...",
    "ecdh_curves": "...",
    "sni": "..."
  }

alpn_protocols
  *(optional, string)* Supplies the list of ALPN protocols that connections should request. In
  practice this is likely to be set to a single value or not set at all:

  * "h2" If upstream connections should use HTTP/2. In the current implementation this must be set
    alongside the *http2* cluster :ref:`features <config_cluster_manager_cluster_features>` option.
    The two options together will use ALPN to tell a server that expects ALPN that Envoy supports
    HTTP/2. Then the *http2* feature will cause new connections to use HTTP/2.

cert_chain_file
  *(optional, string)* The certificate chain file that should be served by the connection. This is
  used to provide a client side TLS certificate to an upstream host.

private_key_file
  *(optional, string)* The private key that corresponds to the certificate chain file.

ca_cert_file
  *(optional, string)* A file containing certificate authority certificates to use in verifying
  a presented server certificate.

verify_certificate_hash
  *(optional, string)* If specified, Envoy will verify (pin) the hash of the presented server
  certificate.

verify_subject_alt_name
  *(optional, array)* An optional list of subject alt names. If specified, Envoy will verify
  that the server certificate's subject alt name matches one of the specified values.

cipher_suites
  *(optional, string)* If specified, the TLS connection will only support the specified `cipher list
  <https://commondatastorage.googleapis.com/chromium-boringssl-docs/ssl.h.html#Cipher-suite-configuration>`_.
  If not specified, the default list:

.. code-block:: none

  [ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]
  [ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]
  ECDHE-ECDSA-AES128-SHA256
  ECDHE-RSA-AES128-SHA256
  ECDHE-ECDSA-AES128-SHA
  ECDHE-RSA-AES128-SHA
  AES128-GCM-SHA256
  AES128-SHA256
  AES128-SHA
  ECDHE-ECDSA-AES256-GCM-SHA384
  ECDHE-RSA-AES256-GCM-SHA384
  ECDHE-ECDSA-AES256-SHA384
  ECDHE-RSA-AES256-SHA384
  ECDHE-ECDSA-AES256-SHA
  ECDHE-RSA-AES256-SHA
  AES256-GCM-SHA384
  AES256-SHA256
  AES256-SHA

will be used.

ecdh_curves
  *(optional, string)* If specified, the TLS connection will only support the specified ECDH curves.
  If not specified, the default curves (X25519, P-256) will be used.

sni
  *(optional, string)* If specified, the string will be presented as the SNI during the TLS
  handshake.
