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
    "cipher_suites": "...",
    "ecdh_curves": "...",
    "session_ticket_key_paths": []
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
  will not be verified. By default, a client certificate is optional, unless one of the additional
  options (
  :ref:`require_client_certificate <config_listener_ssl_context_require_client_certificate>`,
  :ref:`verify_certificate_hash <config_listener_ssl_context_verify_certificate_hash>` or
  :ref:`verify_subject_alt_name <config_listener_ssl_context_verify_subject_alt_name>`) is also
  specified.

.. _config_listener_ssl_context_require_client_certificate:

require_client_certificate
  *(optional, boolean)* If specified, Envoy will reject connections without a valid client certificate.

.. _config_listener_ssl_context_verify_certificate_hash:

verify_certificate_hash
  *(optional, string)* If specified, Envoy will verify (pin) the hash of the presented client
  side certificate.

.. _config_listener_ssl_context_verify_subject_alt_name:

verify_subject_alt_name
  *(optional, array)* An optional list of subject alt names. If specified, Envoy will verify
  that the client certificate's subject alt name matches one of the specified values.

cipher_suites
  *(optional, string)* If specified, the TLS listener will only support the specified `cipher list
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

session_ticket_key_paths
  *(optional, array)* Paths to keyfiles for encrypting and decrypting TLS session tickets. The
  first keyfile in the array contains the key to encrypt all new sessions created by this context.
  All keys are candidates for decrypting received tickets. This allows for easy rotation of keys
  by, for example, putting the new keyfile first, and the previous keyfile second.

  If `session_ticket_key_paths` is not specified, the TLS library will still support resuming
  sessions via tickets, but it will use an internally-generated and managed key, so sessions cannot
  be resumed across hot restarts or on different hosts.

  Each keyfile must contain exactly 80 bytes of cryptographically-secure random data.  For example,
  the output of ``openssl rand 80``.

  .. attention::

    Using this feature has serious security considerations and risks. Improper handling of keys may
    result in loss of secrecy in connections, even if ciphers supporting perfect forward secrecy
    are used. See https://www.imperialviolet.org/2013/06/27/botchingpfs.html for some discussion.
    To minimize the risk, you must:

    * Keep the session ticket keys at least as secure as your TLS certificate private keys
    * Rotate session ticket keys at least daily, and preferably hourly
    * Always generate keys using a cryptographically-secure random data source
