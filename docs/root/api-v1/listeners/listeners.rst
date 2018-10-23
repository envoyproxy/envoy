.. _config_listeners_v1:

Listeners
=========

.. toctree::
  :hidden:

  lds

.. code-block:: json

  {
    "name": "...",
    "address": "...",
    "filters": [],
    "ssl_context": "{...}",
    "bind_to_port": "...",
    "use_proxy_proto": "...",
    "use_original_dst": "...",
    "per_connection_buffer_limit_bytes": "...",
    "drain_type": "..."
  }

.. _config_listeners_name:

name
  *(optional, string)* The unique name by which this listener is known. If no name is provided,
  Envoy will allocate an internal UUID for the listener. If the listener is to be dynamically
  updated or removed via :ref:`LDS <config_listeners_lds>` a unique name must be provided.
  By default, the maximum length of a listener's name is limited to 60 characters. This limit can be
  increased by setting the :option:`--max-obj-name-len` command line argument to the desired value.

address
  *(required, string)* The address that the listener should listen on. Currently only TCP
  listeners are supported, e.g., "tcp://127.0.0.1:80". Note, "tcp://0.0.0.0:80" is the wild card
  match for any IPv4 address with port 80.

:ref:`filters <config_listener_network_filters>`
  *(required, array)* A list of individual :ref:`network filters <arch_overview_network_filters>`
  that make up the filter chain for connections established with the listener. Order matters as the
  filters are processed sequentially as connection events happen.

  **Note:** If the filter list is empty, the connection will close by default.

:ref:`ssl_context <config_listener_ssl_context>`
  *(optional, object)* The :ref:`TLS <arch_overview_ssl>` context configuration for a TLS listener.
  If no TLS context block is defined, the listener is a plain text listener.

bind_to_port
  *(optional, boolean)* Whether the listener should bind to the port. A listener that doesn't bind
  can only receive connections redirected from other listeners that set use_original_dst parameter to
  true. Default is true.

use_proxy_proto
  *(optional, boolean)* Whether the listener should expect a
  `PROXY protocol V1 <http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt>`_ header on new
  connections. If this option is enabled, the listener will assume that that remote address of the
  connection is the one specified in the header. Some load balancers including the AWS ELB support
  this option. If the option is absent or set to false, Envoy will use the physical peer address
  of the connection as the remote address.

use_original_dst
  *(optional, boolean)* If a connection is redirected using *iptables*, the port on which the proxy
  receives it might be different from the original destination address. When this flag is set to true,
  the listener hands off redirected connections to the listener associated with the original
  destination address. If there is no listener associated with the original destination address, the
  connection is handled by the listener that receives it. Defaults to false.

.. _config_listeners_per_connection_buffer_limit_bytes:

per_connection_buffer_limit_bytes
  *(optional, integer)* Soft limit on size of the listener's new connection read and write buffers.
  If unspecified, an implementation defined default is applied (1MiB).

.. _config_listeners_drain_type:

drain_type
  *(optional, string)* The type of draining that the listener does. Allowed values include *default*
  and *modify_only*. See the :ref:`draining <arch_overview_draining>` architecture overview for
  more information.

.. _config_listener_network_filters:

Filters
-------

Network filter :ref:`architecture overview <arch_overview_network_filters>`.

.. code-block:: json

  {
    "name": "...",
    "config": "{...}"
  }

name
  *(required, string)* The name of the filter to instantiate. The name must match a :ref:`supported
  filter <config_network_filters>`.

config
  *(required, object)* Filter specific configuration which depends on the filter being instantiated.
  See the :ref:`supported filters <config_network_filters>` for further documentation.

.. _config_listener_ssl_context:

TLS context
-----------

TLS :ref:`architecture overview <arch_overview_ssl>`.

.. code-block:: json

  {
    "cert_chain_file": "...",
    "private_key_file": "...",
    "alpn_protocols": "...",
    "ca_cert_file": "...",
    "verify_certificate_hash": "...",
    "verify_subject_alt_name": [],
    "crl_file": "...",
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

.. _config_listener_ssl_context_crl_file:

crl_file
  *(optional, string)* An optional `certificate revocation list
  <http://https://en.wikipedia.org/wiki/Certificate_revocation_list>`_ (in PEM format).
  If specified, Envoy will verify that the presented peer certificate has not been revoked by
  this CRL. If this file contains multiple CRLs, all of them will be used.

cipher_suites
  *(optional, string)* If specified, the TLS listener will only support the specified `cipher list
  <https://commondatastorage.googleapis.com/chromium-boringssl-docs/ssl.h.html#Cipher-suite-configuration>`_.
  If not specified, the default list:

.. code-block:: none

  [ECDHE-ECDSA-AES128-GCM-SHA256|ECDHE-ECDSA-CHACHA20-POLY1305]
  [ECDHE-RSA-AES128-GCM-SHA256|ECDHE-RSA-CHACHA20-POLY1305]
  ECDHE-ECDSA-AES128-SHA
  ECDHE-RSA-AES128-SHA
  AES128-GCM-SHA256
  AES128-SHA
  ECDHE-ECDSA-AES256-GCM-SHA384
  ECDHE-RSA-AES256-GCM-SHA384
  ECDHE-ECDSA-AES256-SHA
  ECDHE-RSA-AES256-SHA
  AES256-GCM-SHA384
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

  Each keyfile must contain exactly 80 bytes of cryptographically-secure random data. For example,
  the output of ``openssl rand 80``.

  .. attention::

    Using this feature has serious security considerations and risks. Improper handling of keys may
    result in loss of secrecy in connections, even if ciphers supporting perfect forward secrecy
    are used. See https://www.imperialviolet.org/2013/06/27/botchingpfs.html for some discussion.
    To minimize the risk, you must:

    * Keep the session ticket keys at least as secure as your TLS certificate private keys
    * Rotate session ticket keys at least daily, and preferably hourly
    * Always generate keys using a cryptographically-secure random data source
