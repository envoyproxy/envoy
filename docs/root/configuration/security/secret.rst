.. _config_secret_discovery_service:

Secret discovery service (SDS)
==============================

TLS certificates, the secrets, can be specified in the bootstrap.static_resource
:ref:`secrets <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.StaticResources.secrets>`.
But they can also be fetched remotely by secret discovery service (SDS).

The most important benefit of SDS is to simplify the certificate management. Without this feature, in k8s deployment, certificates must be created as secrets and mounted into the proxy containers. If certificates are expired, the secrets need to be updated and the proxy containers need to be re-deployed. With SDS, a central SDS server will push certificates to all Envoy instances. If certificates are expired, the server just pushes new certificates to Envoy instances, Envoy will use the new ones right away without re-deployment.

If a listener server certificate needs to be fetched by SDS remotely, it will NOT be marked as active, its port will not be opened before the certificates are fetched. If Envoy fails to fetch the certificates due to connection failures, or bad response data, the listener will be marked as active, and the port will be open, but the connection to the port will be reset.

Upstream clusters are handled in a similar way, if a cluster client certificate needs to be fetched by SDS remotely, it will NOT be marked as active and it will not be used before the certificates are fetched. If Envoy fails to fetch the certificates due to connection failures, or bad response data, the cluster will be marked as active, it can be used to handle the requests, but the requests routed to that cluster will be rejected.

If a static cluster is using SDS, and it needs to define a SDS cluster (unless Google gRPC is used which doesn't need a cluster), the SDS cluster has to be defined before the static clusters using it.

The connection between Envoy proxy and SDS server has to be secure. One option is to run the SDS server on the same host and use Unix Domain Socket for the connection. Otherwise the connection requires TLS with authentication between the proxy and SDS server. Credential types in use today for authentication are:

* mTLS -- In this case, the client certificates for the SDS connection must be statically configured.
* AWS IAM SigV4

SDS server
----------

A SDS server needs to implement the gRPC service :repo:`SecretDiscoveryService <api/envoy/service/secret/v3/sds.proto>`.
It follows the same protocol as other :ref:`xDS <xds_protocol>`.

.. note::

    `SPIRE <https://spiffe.io/docs/latest/spire-about/>`_,
    an open-source reference implementation of the
    `SPIFFE specification for production identity <https://spiffe.io/>`_,
    can act as an SDS server for Envoy. See the
    `SPIRE documentation <https://spiffe.io/docs/latest/microservices/envoy/>`_
    for how to configure Envoy with SPIFFE and SPIRE.


SDS Configuration
-----------------

:ref:`SdsSecretConfig <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.SdsSecretConfig>` is used to specify the secret. Its field *name* is a required field. If its *sds_config* field is empty, the *name* field specifies the secret in the bootstrap static_resource :ref:`secrets <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.StaticResources.secrets>`. Otherwise, it specifies the SDS server as :ref:`ConfigSource <envoy_v3_api_msg_config.core.v3.ConfigSource>`. Only gRPC is supported for the SDS service so its *api_config_source* must specify a **grpc_service**.

*SdsSecretConfig* is used in two fields in :ref:`CommonTlsContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CommonTlsContext>`. The first field is *tls_certificate_sds_secret_configs* to use SDS to get :ref:`TlsCertificate <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.TlsCertificate>`. The second field is *validation_context_sds_secret_config* to use SDS to get :ref:`CertificateValidationContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CertificateValidationContext>`.

.. _sds_key_rotation:

Key rotation
------------

It's usually preferrable to perform key rotation via gRPC SDS, but when this is not possible or
desired (e.g. during bootstrap of SDS credentials), SDS allows for filesystem rotation when secrets
refer to filesystem paths. This currently is supported for the following secret types:

* :ref:`TlsCertificate <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.TlsCertificate>`
* :ref:`CertificateValidationContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CertificateValidationContext>`

By default, directories containing secrets are watched for filesystem move events. For example, a
key or trusted CA certificates at ``/foo/bar/baz/cert.pem`` will be watched at ``/foo/bar/baz``.
Explicit control over the watched directory is possible by specifying a *watched_directory* path in
:ref:`TlsCertificate
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.TlsCertificate.watched_directory>` and
:ref:`CertificateValidationContext
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.watched_directory>`.
This allows watches to be established at path predecessors, e.g. ``/foo/bar``; this capability is
useful when implementing common key rotation schemes.

An example of key rotation is provided :ref:`below <xds_certificate_rotation>`.

Example one: static_resource
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
        load_assignment:
          cluster_name: local_service_tls
          ...
          transport_socket:
            name: envoy.transport_sockets.tls
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
              common_tls_context:
                tls_certificate_sds_secret_configs:
                - name: client_cert
    listeners:
      ....
      filter_chains:
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:
              tls_certificate_sds_secret_configs:
              - name: server_cert
              validation_context_sds_secret_config:
                name: validation_context


In this example, certificates are specified in the bootstrap static_resource, they are not fetched remotely. In the config, *secrets* static resource has 3 secrets: **client_cert**, **server_cert** and **validation_context**. In the cluster config, one of hosts uses **client_cert** in its *tls_certificate_sds_secret_configs*. In the listeners section, one of them uses **server_cert** in its *tls_certificate_sds_secret_configs* and **validation_context** for its *validation_context_sds_secret_config*.

.. _sds_server_example:

Example two: SDS server
------------------------

This example shows how to configure secrets fetched from remote SDS servers:

.. literalinclude:: _include/sds-source-example.yaml
   :language: yaml
   :lines: 1-97
   :linenos:
   :lineno-start: 1
   :caption: :download:`sds-source-example.yaml <_include/sds-source-example.yaml>`

For illustration, above example uses three methods to access the SDS server. A gRPC SDS server can be reached by Unix Domain Socket path **/tmp/uds_path** and **127.0.0.1:8234** by mTLS. It provides three secrets, **client_cert**, **server_cert** and **validation_context**. In the config, cluster **example_cluster** certificate **client_cert** is configured to use Google gRPC with UDS to talk to the SDS server. The Listener needs to fetch **server_cert** and **validation_context** from the SDS server. The **server_cert** is using Envoy gRPC with cluster **sds_server_mtls** configured with client certificate to use mTLS to talk to SDS server. The **validate_context** is using Envoy gRPC with cluster **sds_server_uds** configured with UDS path to talk to the SDS server.

.. _xds_certificate_rotation:

Example three: certificate rotation for xDS gRPC connection
------------------------------------------------------------

Managing certificates for xDS gRPC connection between Envoy and xDS server introduces a bootstrapping problem: SDS server cannot manage certificates that are required to connect to the server.

This example shows how to set up xDS connection by sourcing SDS configuration from the filesystem.
The certificate and key files are watched with inotify and reloaded automatically without restart.
In contrast, :ref:`sds_server_example` requires a restart to reload xDS certificates and key after update.

.. code-block:: yaml

    clusters:
    - name: control_plane
      type: LOGICAL_DNS
      connect_timeout: 1s
      load_assignment:
        cluster_name: control_plane
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: controlplane
                  port_value: 8443
      typed_extension_protocol_options:
        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
          explicit_http_config:
            http2_protocol_options: {}
      transport_socket:
        name: "envoy.transport_sockets.tls"
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext"
          common_tls_context:
            tls_certificate_sds_secret_configs:
              name: tls_sds
              sds_config:
                path: /etc/envoy/tls_certificate_sds_secret.yaml
            validation_context_sds_secret_config:
              name: validation_context_sds
              sds_config:
                path: /etc/envoy/validation_context_sds_secret.yaml

Paths to client certificate, including client's certificate chain and private key are given in SDS config file ``/etc/envoy/tls_certificate_sds_secret.yaml``:

.. code-block:: yaml

    resources:
      - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
        name: tls_sds
        tls_certificate:
          certificate_chain:
            filename: /certs/sds_cert.pem
          private_key:
            filename: /certs/sds_key.pem

Path to CA certificate bundle for validating the xDS server certificate is given in SDS config file ``/etc/envoy/validation_context_sds_secret.yaml``:

.. code-block:: yaml

    resources:
      - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
        name: validation_context_sds
        validation_context:
          trusted_ca:
            filename: /certs/cacert.pem

In the above example, a watch will be established on ``/certs``. File movement in this directory
will trigger an update. An alternative common key rotation scheme that provides improved atomicity
is to establish an active symlink ``/certs/current`` and use an atomic move operation to replace the
symlink. The watch in this case needs to be on the certificate's grandparent directory. Envoy
supports this scheme via the use of *watched_directory*. Continuing the above examples:

.. code-block:: yaml

    resources:
      - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
        name: tls_sds
        tls_certificate:
          certificate_chain:
            filename: /certs/current/sds_cert.pem
          private_key:
            filename: /certs/current/sds_key.pem
          watched_directory:
            path: /certs

.. code-block:: yaml

    resources:
      - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
        name: validation_context_sds
        validation_context:
          trusted_ca:
            filename: /certs/current/cacert.pem
          watched_directory:
            path: /certs

Secret rotation can be performed with:

.. code-block:: bash

  ln -s <path to new secrets> /certs/new && mv -Tf /certs/new /certs/current

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

SDS has a :ref:`statistics <subscription_statistics>` tree rooted in the *sds.<SECRET_NAME>.*
namespace. In addition, the following statistics are tracked in this namespace:

.. csv-table::
     :header: Name, Description
     :widths: 1, 2

     key_rotation_failed, Total number of filesystem key rotations that failed outside of an SDS update.
