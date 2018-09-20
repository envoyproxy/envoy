.. _config_secret_discovery_service:

Secret discovery service (SDS)
==============================

TLS certificates, the secrets, can be specified in the bootstrap.static_resource
:ref:`secrets <envoy_api_field_config.bootstrap.v2.Bootstrap.StaticResources.secrets>`.
But they can also be fetched remotely by secret discovery service (SDS).

The benefits of remote fetch are

* Easy to rotate expired certificates. SDS servers just push new certificates to Envoy.
* Certificates are safe; they are not build into images, they can be fetched by secure channels such as Unix Domain Socket.

SDS server
----------

A SDS server needs to implement the gRPC service `SecretDiscoveryService <https://github.com/envoyproxy/envoy/blob/master/api/envoy/service/discovery/v2/sds.proto>`_.
It follows the same protocol as other `xDS <https://github.com/envoyproxy/data-plane-api/blob/master/XDS_PROTOCOL.md>`_

SDS Configuration
-----------------

:ref:`SdsSecretConfig <envoy_api_msg_auth.SdsSecretConfig>` is used to specify the secret. Its field "name" is a required field. If its "sds_config" field is empty, the "name" field specifies the secret in the bootstrap static_resource :ref:`secrets <envoy_api_field_config.bootstrap.v2.Bootstrap.StaticResources.secrets>`. Otherwise, it specifies the SDS server as :ref:`ConfigSource <envoy_api_msg_core.ConfigSource>`. Only gRPC is supported for the SDS service so its "api_config_source" must specify a "grpc_service".

SdsSecretConfig is used in two fields in :ref:`CommonTlsContext <envoy_api_msg_auth.CommonTlsContext>`. The first field is "tls_certificate_sds_secret_configs" to use SDS to get :ref:`TlsCertificate <envoy_api_msg_auth.TlsCertificate>`. The second field is "validation_context_sds_secret_config" to use SDS to get :ref:`CertificateValidationContext <envoy_api_msg_auth.CertificateValidationContext>`.

If a listener or a cluster requires TLS certificates that needed to be fetched by SDS, it will NOT be marked as active before they are fetched. If Envoy fails to fetch the certificates due to connection failures, or bad response data, the listener will be marked as active, but the connection to the port will be reset. For the cluster, it will be marked as active too, but the requests routed to that cluster will be rejected.

Examples one: static_resource
-----------------------------

This example show how to configurate secrets in the static_resource:

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

Examples two: SDS server
------------------------

This example shows how to configurate secrets fetched from remote SDS server:

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


In the above example, a gRPC SDS server can be reached by Unix Domain Socket path "/tmp/uds_path". It can provide three secrets, "client_cert", "server_cert" and "validation_context". In the config, "server_cert" and "validation_context" are used by one of listeners and "client_cert" is used by one of clusters.

For illustration purpose, the config is using Google grpc to fetch "client_cert" and "server_cert" secrets from the SDS server. It uses Envoy grpc to fetch "validation_context" secret. In order to use Envoy gRPC, a static cluster is needed to specify the SDS gRPC server.
