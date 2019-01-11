.. _config_secret_discovery_service:

Secret discovery service (SDS)
==============================

TLS certificates, the secrets, can be specified in the bootstrap.static_resource
:ref:`secrets <envoy_api_field_config.bootstrap.v2.Bootstrap.StaticResources.secrets>`.
But they can also be fetched remotely by secret discovery service (SDS).

The most important benefit of SDS is to simplify the certificate management. Without this feature, in k8s deployment, certificates must be created as secrets and mounted into the proxy containers. If certificates are expired, the secrets need to be updated and the proxy containers need to be re-deployed. With SDS, a central SDS server will push certificates to all Envoy instances. If certificates are expired, the server just pushes new certificates to Envoy instances, Envoy will use the new ones right away without re-deployment.

If a listener server certificate needs to be fetched by SDS remotely, it will NOT be marked as active, its port will not be opened before the certificates are fetched. If Envoy fails to fetch the certificates due to connection failures, or bad response data, the listener will be marked as active, and the port will be open, but the connection to the port will be reset.

Upstream clusters are handled in a similar way, if a cluster client certificate needs to be fetched by SDS remotely, it will NOT be marked as active and it will not be used before the certificates are fetched. If Envoy fails to fetch the certificates due to connection failures, or bad response data, the cluster will be marked as active, it can be used to handle the requests, but the requests routed to that cluster will be rejected.

If a static cluster is using SDS, and it needs to define a SDS cluster (unless Google gRPC is used which doens't need a cluster), the SDS cluster has to be defined before the static clusters using it.

The connection bewteeen Envoy proxy and SDS server has to be secure. One option is to run the SDS server on the same host and use Unix Domain Socket for the connection. Otherwise it requires mTLS between the proxy and SDS server. In this case, the client certificates for the SDS connection must be statically configured.

SDS server
----------

A SDS server needs to implement the gRPC service `SecretDiscoveryService <https://github.com/envoyproxy/envoy/blob/master/api/envoy/service/discovery/v2/sds.proto>`_.
It follows the same protocol as other `xDS <https://github.com/envoyproxy/data-plane-api/blob/master/XDS_PROTOCOL.md>`_

SDS Configuration
-----------------

:ref:`SdsSecretConfig <envoy_api_msg_auth.SdsSecretConfig>` is used to specify the secret. Its field *name* is a required field. If its *sds_config* field is empty, the *name* field specifies the secret in the bootstrap static_resource :ref:`secrets <envoy_api_field_config.bootstrap.v2.Bootstrap.StaticResources.secrets>`. Otherwise, it specifies the SDS server as :ref:`ConfigSource <envoy_api_msg_core.ConfigSource>`. Only gRPC is supported for the SDS service so its *api_config_source* must specify a **grpc_service**.

*SdsSecretConfig* is used in two fields in :ref:`CommonTlsContext <envoy_api_msg_auth.CommonTlsContext>`. The first field is *tls_certificate_sds_secret_configs* to use SDS to get :ref:`TlsCertificate <envoy_api_msg_auth.TlsCertificate>`. The second field is *validation_context_sds_secret_config* to use SDS to get :ref:`CertificateValidationContext <envoy_api_msg_auth.CertificateValidationContext>`.

Examples one: static_resource
-----------------------------

This example show how to configure secrets in the static_resource:

.. code-block:: yaml

  static_resources:
    secrets:
      - name: server_cert
        tls_certificate:
          certificate_chain:
            filename: certs/servercert.pem
          private_key:
            filename: certs/serverkey.pem
      - name: client_cert
        tls_certificate:
          certificate_chain:
            filename: certs/clientcert.pem
          private_key:
            filename: certs/clientkey.pem
      - name: validation_context
        validation_context:
          trusted_ca:
            filename: certs/cacert.pem
          verify_certificate_hash:
            E0:F3:C8:CE:5E:2E:A3:05:F0:70:1F:F5:12:E3:6E:2E:97:92:82:84:A2:28:BC:F7:73:32:D3:39:30:A1:B6:FD
    clusters:
      - connect_timeout: 0.25s
        hosts:
        - name: local_service_tls
          ...
          tls_context:
            common_tls_context:
              tls_certificate_sds_secret_configs:
              - name: client_cert
    listeners:
      ....
      filter_chains:
        tls_context:
          common_tls_context:
            tls_certificate_sds_secret_configs:
            - name: server_cert
            validation_context_sds_secret_config:
              name: validation_context


In this example, certificates are specified in the bootstrap static_resource, they are not fetched remotely. In the config, *secrets* static resource has 3 secrets: **client_cert**, **server_cert** and **validation_context**. In the cluster config, one of hosts uses **client_cert** in its *tls_certificate_sds_secret_configs*. In the listeners section, one of them uses **server_cert** in its *tls_certificate_sds_secret_configs* and **validation_context** for its *validation_context_sds_secret_config*.

Examples two: SDS server
------------------------

This example shows how to configure secrets fetched from remote SDS servers:

.. code-block:: yaml

    clusters:
      - name: sds_server_mtls
        http2_protocol_options: {}
        hosts:
          socket_address:
            address: 127.0.0.1
            port_value: 8234
        tls_context:
          common_tls_context:
          - tls_certificate:
            certificate_chain:
              filename: certs/sds_cert.pem
            private_key:
              filename: certs/sds_key.pem
      - name: sds_server_uds
        http2_protocol_options: {}
        hosts:
          - pipe:
              path: /tmp/uds_path
      - name: example_cluster
        connect_timeout: 0.25s
        hosts:
        - name: local_service_tls
          ...
          tls_context:
            common_tls_context:
              tls_certificate_sds_secret_configs:
              - name: client_cert
                sds_config:
                  api_config_source:
                    api_type: GRPC
                    grpc_services:
                      google_grpc:
                        target_uri: unix:/tmp/uds_path
    listeners:
      ....
      filter_chains:
        tls_context:
          common_tls_context:
            tls_certificate_sds_secret_configs:
            - name: server_cert
              sds_config:
                api_config_source:
                  api_type: GRPC
                  grpc_services:
                    envoy_grpc:
                      cluster_name: sds_server_mtls
            validation_context_sds_secret_config:
              name: validation_context
              sds_config:
                api_config_source:
                  api_type: GRPC
                  grpc_services:
                    envoy_grpc:
                      cluster_name: sds_server_uds


For illustration, above example uses three methods to access the SDS server. A gRPC SDS server can be reached by Unix Domain Socket path **/tmp/uds_path** and **127.0.0.1:8234** by mTLS. It provides three secrets, **client_cert**, **server_cert** and **validation_context**. In the config, cluster **example_cluster** certificate **client_cert** is configured to use Google gRPC with UDS to talk to the SDS server. The Listener needs to fetch **server_cert** and **validation_context** from the SDS server. The **server_cert** is using Envoy gRPC with cluster **sds_server_mtls** configured with client certificate to use mTLS to talk to SDS server. The **validate_context** is using Envoy gRPC with cluster **sds_server_uds** configured with UDS path to talk to the SDS server.

Statistics
----------
SSL socket factory outputs following SDS related statistics. They are all counter type.

For downstream listeners, they are in the *listener.<LISTENER_IP>.server_ssl_socket_factory.* namespace.

.. csv-table::
     :header: Name, Description
     :widths: 1, 2

     ssl_context_update_by_sds, Total number of ssl context has been updated.
     downstream_context_secrets_not_ready, Total number of downstream connections reset due to empty ssl certificate.

For upstream clusters, they are in the *cluster.<CLUSTER_NAME>.client_ssl_socket_factory.* namespace.

.. csv-table::
     :header: Name, Description
     :widths: 1, 2

     ssl_context_update_by_sds, Total number of ssl context has been updated.
     upstream_context_secrets_not_ready, Total number of upstream connections reset due to empty ssl certificate.

