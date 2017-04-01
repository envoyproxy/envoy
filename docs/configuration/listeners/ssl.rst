.. _config_listener_ssl_context:

TLS context
===========

TLS :ref:`architecture overview <arch_overview_ssl>`.

.. code-block:: json

  {
    "cert_chain_file": "...",
    "private_key_file": "...",
    "alpn_protocols": "...",
    "alt_alpn_protocols": "...",
    "ca_cert_file": "...",
    "verify_certificate_hash": "...",
    "verify_subject_alt_name": [],
    "cipher_suites": "..."
  }

cert_chain_file
  *(required, string)* The certificate chain file that should be served by the listener.

private_key_file
  *(required, string)* The private key that corresponds to the certificate chain file.

alpn_protocols
  *(optional, string)* Supplies the list of ALPN protocols that the listener should expose. In
  practice this is likely to be set to one of two values (see the
  :ref:`codec_type <config_http_conn_man_codec_type>` parameter in the HTTP connection
  manager for more information):

  * "h2,http/1.1" If the listener is going to support both HTTP/2 and HTTP/1.1.
  * "http/1.1" If the listener is only going to support HTTP/1.1

.. _config_listener_ssl_context_alt_alpn:

alt_alpn_protocols
  *(optional, string)* An alternate ALPN protocol string that can be switched to via runtime. This
  is useful for example to disable HTTP/2 without having to deploy a new configuration.

ca_cert_file
  *(optional, string)* A file containing certificate authority certificates to use in verifying
  a presented client side certificate. If not specified and a client certificate is presented it
  will not be verified.

verify_certificate_hash
  *(optional, string)* If specified, Envoy will verify (pin) the hash of the presented client
  side certificate.

verify_subject_alt_name
  *(optional, array)* An optional list of subject alt names. If specified, Envoy will verify
  that the server certificate's subject alt name matches one of the specified values.

cipher_suites
  *(optional, string)* If specified, the TLS listener will only support the specified cipher list.
  If not specified, a default list will be used.
