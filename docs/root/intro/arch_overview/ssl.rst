.. _arch_overview_ssl:

TLS
===

Envoy supports both :ref:`TLS termination <envoy_api_field_listener.FilterChain.tls_context>` in listeners as well as
:ref:`TLS origination <envoy_api_field_Cluster.tls_context>` when making connections to upstream
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

.. _secret_discovery_service:

Secret discovery service (SDS)
------------------------------

TLS certificates can be specified in the bootstrap.static_resource
:ref:`secrets <envoy_api_field_config.bootstrap.v2.Bootstrap.StaticResources.secrets>`.
But they can also be fetched remotely by Secret discovery service (SDS).

The benefits of remote fetch are

* Easy to rotate expired certificates. SDS servers just push new certificates to Envoy.
* Certificates are safe; they are not build into images, they can be fetched by secure channels such as Unix Domain Socket.

A SDS server needs to implement service `SecretDiscoveryService <https://github.com/envoyproxy/envoy/blob/master/api/envoy/service/discovery/v2/sds.proto>`_.
It follows the same protocol as other `xDS <https://github.com/envoyproxy/data-plane-api/blob/master/XDS_PROTOCOL.md>`_

:ref:`SdsSecretConfig <envoy_api_msg_auth.SdsSecretConfig>` is used to specify the secret. Its field "name" is a required field. If its "sds_config" field is empty, the "name" field specifies the secret in the bootstrap static_resource :ref:`secrets <envoy_api_field_config.bootstrap.v2.Bootstrap.StaticResources.secrets>`. Otherwise, it specifies the SDS server as :ref:`ConfigSource <envoy_api_msg_core.ConfigSource>`. Only gRPC is supported for the SDS service so its "api_config_source" must specify a "grpc_service".

SdsSecretConfig is used in two fields in :ref:`CommonTlsContext <envoy_api_msg_auth.CommonTlsContext>`. The first field is "tls_certificate_sds_secret_configs" to use SDS to get :ref:`TlsCertificate <envoy_api_msg_auth.TlsCertificate>`. The second field is "validation_context_sds_secret_config" to use SDS to get :ref:`CertificateValidationContext <envoy_api_msg_auth.CertificateValidationContext>`.

If a listener or a cluster requires TLS certificates that needed to be fetched by SDS, it will NOT be marked as active before they are fetched.  If Envoy fails to fetch the certificates due to connection failures, or bad response data, the listener will be marked as active, but the connection to the port will be reset. For the cluster, it will be marked as active too, but the requests routed to that cluster will be rejected.

Following examples show how to configurate SDS in the config:

.. code-block:: yaml

  static_resources:
    secrets:
      - name: "server_cert"
        tls_certificate:
          certificate_chain:
            filename: "certs/servercert.pem"
          private_key:
            filename: "certs/serverkey.pem"
      - name: "client_cert"
        tls_certificate:
          certificate_chain:
            filename: "certs/clientcert.pem"
          private_key:
            filename: "certs/clientkey.pem"
      - name: "validation_context"
        validation_context:
          trusted_ca:
            filename: "certs/cacert.pem"
          verify_certificate_hash:
            "E0:F3:C8:CE:5E:2E:A3:05:F0:70:1F:F5:12:E3:6E:2E:97:92:82:84:A2:28:BC:F7:73:32:D3:39:30:A1:B6:FD"
    clusters:
      - connect_timeout: 0.25s
        hosts:
        - name: local_service_tls
          ...
          tls_context:
            common_tls_context:
              tls_certificate_sds_secret_configs:
              - name: "client_cert"
    listeners:
      ....
      filter_chains:
        tls_context:
          common_tls_context:
            tls_certificate_sds_secret_configs:
            - name: "server_cert"
            validation_context_sds_secret_config:
            - name: "validation_context"


In this example, certificates are specified in the bootstrap static_resource, they are not fetched remotely. In the config, "secrets" static resource has 3 secrets: "client_cert", "server_cert" and "validation_context". In the cluster config, one of hosts uses "client_cert" in its tls_certificate_sds_secret_configs. In the listeners section, one of them uses "server_cert" in its tls_certificate_sds_secret_configs and "validation_context" for its validation_context_sds_secret_config.


.. code-block:: yaml

    clusters:
      - name: example_cluster
        connect_timeout: 0.25s
        hosts:
        - name: local_service_tls
          ...
          tls_context:
            common_tls_context:
              tls_certificate_sds_secret_configs:
              - name: "client_cert"
                sds_config:
                  api_config_source:
                    api_type: GRPC
                    grpc_services:
                    - google_grpc:
                      target_uri: unix:/tmp/uds_path
      - name: sds_server
        http2_protocol_options: {}
        hosts:
          - pipe:
              path: /tmp/uds_path
    listeners:
      ....
      filter_chains:
        tls_context:
          common_tls_context:
            tls_certificate_sds_secret_configs:
            - name: "server_cert"
              sds_config:
                api_config_source:
                  api_type: GRPC
                  grpc_services:
                  - google_grpc:
                    target_uri: unix:/tmp/uds_path
            validation_context_sds_secret_config:
            - name: "validation_context"
              sds_config:
                api_config_source:
                  api_type: GRPC
                  grpc_services:
                  - envoy_grpc:
                    cluster_name: sds_server


In the above example, a gRPC SDS server can be reached by Unix Domain Socket path "/tmp/uds_path". It can provide three secrets, "client_cert", "server_cert" and "validation_context".  In the config, "server_cert" and "validation_context" are used by one of listeners and "client_cert" is used by one of clusters.

For illustration purpose, the config is using Google grpc to fetch "client_cert" and "server_cert" secrets from the SDS server. It uses Envoy grpc to fetch "validation_context" secret. In order to use Envoy gRPC, a static cluster is needed to specify the SDS gRPC server.

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
