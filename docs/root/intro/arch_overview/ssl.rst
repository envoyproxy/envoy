.. _arch_overview_ssl:

TLS
===

Envoy supports both :ref:`TLS termination <config_listener_ssl_context>` in listeners as well as
:ref:`TLS origination <config_cluster_manager_cluster_ssl>` when making connections to upstream
clusters. Support is sufficient for Envoy to perform standard edge proxy duties for modern web
services as well as to initiate connections with external services that have advanced TLS
requirements (TLS1.2, SNI, etc.). Envoy supports the following TLS features:

* **Configurable ciphers**: Each TLS listener and client can specify the ciphers that it supports.
* **Client certificates**: Upstream/client connections can present a client certificate in addition
  to server certificate verification.
* **Certificate verification and pinning**: Certificate verification options include basic chain
  verification, subject name verification, and hash pinning.
* **Certificate revocation**: Envoy can check peer certificates against a certificate revocation list
  (CRL) if one is :ref:`provided <envoy_api_field_auth.CertificateValidationContext.crl>`.
* **ALPN**: TLS listeners support ALPN. The HTTP connection manager uses this information (in
  addition to protocol inference) to determine whether a client is speaking HTTP/1.1 or HTTP/2.
* **SNI**: SNI is supported for both server (listener) and client (upstream) connections.
* **Session resumption**: Server connections support resuming previous sessions via TLS session
  tickets (see `RFC 5077 <https://www.ietf.org/rfc/rfc5077.txt>`_). Resumption can be performed
  across hot restarts and between parallel Envoy instances (typically useful in a front proxy
  configuration).

Underlying implementation
-------------------------

Currently Envoy is written to use `BoringSSL <https://boringssl.googlesource.com/boringssl>`_ as the
TLS provider.

.. _arch_overview_ssl_enabling_verification:

Enabling certificate verification
---------------------------------

Certificate verification of both upstream and downstream connections is not enabled unless the
validation context specifies one or more trusted authority certificates.

Example configuration
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: yaml

  static_resources:
    listeners:
    - name: listener_0
      address: { socket_address: { address: 127.0.0.1, port_value: 10000 } }
      filter_chains:
      - filters:
        - name: envoy.http_connection_manager
          # ...
        tls_context:
          common_tls_context:
            validation_context:
              trusted_ca:
                filename: /usr/local/my-client-ca.crt
    clusters:
    - name: some_service
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      hosts: [{ socket_address: { address: 127.0.0.2, port_value: 1234 }}]
      tls_context:
        common_tls_context:
          tls_certificates:
            certificate_chain: { "filename": "/cert.crt" }
            private_key: { "filename": "/cert.key" }
          validation_context:
            trusted_ca:
              filename: /etc/ssl/certs/ca-certificates.crt

*/etc/ssl/certs/ca-certificates.crt* is the default path for the system CA bundle on Debian systems.
This makes Envoy verify the server identity of *127.0.0.2:1234* in the same way as e.g. cURL does on
standard Debian installations. Common paths for system CA bundles on Linux and BSD are

* /etc/ssl/certs/ca-certificates.crt (Debian/Ubuntu/Gentoo etc.)
* /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem (CentOS/RHEL 7)
* /etc/pki/tls/certs/ca-bundle.crt (Fedora/RHEL 6)
* /etc/ssl/ca-bundle.pem (OpenSUSE)
* /usr/local/etc/ssl/cert.pem (FreeBSD)
* /etc/ssl/cert.pem (OpenBSD)

See the reference for :ref:`UpstreamTlsContexts <envoy_api_msg_auth.UpstreamTlsContext>` and
:ref:`DownstreamTlsContexts <envoy_api_msg_auth.DownstreamTlsContext>` for other TLS options.

.. _arch_overview_ssl_auth_filter:

Authentication filter
---------------------

Envoy provides a network filter that performs TLS client authentication via principals fetched from
a REST VPN service. This filter matches the presented client certificate hash against the principal
list to determine whether the connection should be allowed or not. Optional IP white listing can
also be configured. This functionality can be used to build edge proxy VPN support for web
infrastructure.

Client TLS authentication filter :ref:`configuration reference
<config_network_filters_client_ssl_auth>`.
