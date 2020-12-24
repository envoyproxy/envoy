.. _config_secret_discovery_service:

证书（Secret）发现服务（SDS）
=============================

TLS 证书，secrets，可以在 bootstrap.static_resource :ref:`secrets <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.StaticResources.secrets>` 中指定。但是也可以通过 secret 发现服务（SDS）来远程获取。

SDS 最重要的好处就是简化了证书管理。如果没有这个特性，在 k8s deployment 中，证书就必须以 secret 的方式被创建，然后挂载进代理容器。如果证书过期了，就需要更新 secret 且代理容器需要被重新部署。如果使用 SDS，一个集中的 SDS 服务器会将证书推送给所有的 Envoy 实例。如果证书过期了，服务器仅需要将新证书推送至 Envoy 实例，Envoy 将会立即使用新证书且不需要重新部署代理容器。

如果需要通过 SDS 来远程获取一个监听器服务器的证书，此监听器将不会被标记为激活状态，在证书被获取之前，它的端口将不会被打开。如果因为连接失败或者错误的返回数据导致 Envoy 获取证书失败，监听器将会被标记为激活状态，而且端口也会被打开，但是此端口的连接将会被重置。

上游集群的处理方式类似，如果需要通过 SDS 来远程获取一个集群客户端证书，集群将不会被标记为激活状态，在证书被获取之前，集群处于不可用状态。如果因为连接失败或者错误的返回数据导致 Envoy 获取证书失败，集群将会被标记为激活状态，它依旧可以用来处理请求，但是路由到集群的请求将会被拒绝。

如果一个静态集群正在使用 SDS，集群需要定义一个 SDS 集群（除非使用了并不需要集群的 Google gRPC），SDS 集群需要在静态集群使用 SDS 之前先行对其进行定义。

Envoy 代理和 SDS 服务器之间的连接必须要是安全的。一个选择就是在同一个主机上运行 SDS 服务器，且使用 Unix 域套接字。否则，在代理和 SDS 服务器之间的连接需要 TLS 认证。如今使用的认证凭证都有：

* mTLS -- 在这种情况下，SDS 连接的客户端证书必须被静态配置。
* AWS IAM SigV4

SDS 服务器
-----------

SDS 服务器需要实现 gRPC 服务 :repo:`SecretDiscoveryService <api/envoy/service/secret/v3/sds.proto>` 。它和其它 :ref:`xDS <xds_protocol>` 遵循相同的协议。

SDS 配置
---------

:ref:`SdsSecretConfig <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.SdsSecretConfig>` 用来指定 secret。它的 *name* 字段是一个必填字段。如果 *sds_config* 字段为空，*name* 字段指定了 bootstrap static_resource :ref:`secrets <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.StaticResources.secrets>` 中的 secret。否则，将指定 SDS 服务器为 :ref:`ConfigSource <envoy_v3_api_msg_config.core.v3.ConfigSource>` 。SDS 服务只支持 gRPC，所以 *api_config_source* 字段必须指定为 **grpc_service** 。

*SdsSecretConfig* 在 :ref:`CommonTlsContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CommonTlsContext>` 的两个字段中有所使用。第一个字段 *tls_certificate_sds_secret_configs* 使用 SDS 来获取 :ref:`TlsCertificate <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.TlsCertificate>` 。第二个字段 *validation_context_sds_secret_config* 使用 SDS 来获取 :ref:`CertificateValidationContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CertificateValidationContext>` 。

示例一：static_resource
------------------------

此示例演示了如何在 static_resource 中配置 secret：

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


在这个例子中，在 bootstrap static_resource 中指定了证书，这些是不能够被远程获取的。在配置中，*secrets* 静态资源有 3 个 secret： **client_cert** 、 **server_cert** 和 **validation_context** 。在集群配置中，其中一个主机在它的  *tls_certificate_sds_secret_configs* 中使用 **client_cert** 。在监听器章节，其中一个主机为了 *validation_context_sds_secret_config* ，在它的 *tls_certificate_sds_secret_configs* 和 **validation_context** 中使用了 **server_cert** 。

.. _sds_server_example:

示例二：SDS 服务器
-------------------

此示例演示了如何配置从远端的 SDS 服务器获取到的 secret：

.. code-block:: yaml

    clusters:
      - name: sds_server_mtls
        http2_protocol_options:
          connection_keepalive:
            interval: 30s
            timeout: 5s
        load_assignment:
          cluster_name: sds_server_mtls
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 8234
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
            common_tls_context:
            - tls_certificate:
              certificate_chain:
                filename: certs/sds_cert.pem
              private_key:
                filename: certs/sds_key.pem
      - name: sds_server_uds
        http2_protocol_options: {}
        load_assignment:
          cluster_name: sds_server_uds
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  pipe:
                    path: /tmp/uds_path
      - name: example_cluster
        connect_timeout: 0.25s
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
                  sds_config:
                    api_config_source:
                      api_type: GRPC
                      grpc_services:
                        google_grpc:
                          target_uri: unix:/tmp/uds_path
    listeners:
      ....
      filter_chains:
      - transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
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


为了说明，上述示例使用三种方法来访问 SDS 服务器。一个 gRPC SDS 服务器可以通过 mTLS 来使用 Unix 域套接字路径 **/tmp/uds_path** 和 **127.0.0.1:8234** 进行访问。它提供了三个 secret：**client_cert** 、**server_cert** 和 **validation_context**。在配置中，集群 **example_cluster** 证书 **client_cert** 使用带有 UDS 的 Google gRPC 来和 SDS 服务器通话。监听器需要从 SDS 服务器获取 **server_cert** 和 **validation_context** 。**server_cert** 使用集群 **sds_server_mtls** 的 Envoy gRPC 来通过 mTLS 和 SDS 服务器通信，而此集群配置了客户端证书。 **validate_context** 使用集群 **sds_server_uds** 的 Envoy gRPC 来和 SDS 服务器通信，而此集群配置了 UDS 路径。

.. _xds_certificate_rotation:

示例三：xDS gRPC 连接的证书轮换
--------------------------------

Envoy 和 xDS 服务器之间 xDS gRPC 连接的证书管理道出了一个自举问题：SDS 服务器不能够管理那些需要连接到服务器的证书。

此示例演示了如何使用文件系统的 SDS 配置来设置 xDS 连接。使用 inotify 来监视证书和私钥文件，切无须重启即可自动重新加载。相反地，在xDS 证书和私钥文件在更新以后，:ref:`sds_server_example` 需要通过重启来加载 xDS 证书和私钥文件。

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
      http2_protocol_options: {}
      transport_socket:
        name: "envoy.transport_sockets.tls"
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext"
          common_tls_context:
            tls_certificate_sds_secret_configs:
              sds_config:
                path: /etc/envoy/tls_certificate_sds_secret.yaml
            validation_context_sds_secret_config:
              sds_config:
                path: /etc/envoy/validation_context_sds_secret.yaml

客户端证书路径，包括客户端证书链和在 SDS 配置文件 ``/etc/envoy/tls_certificate_sds_secret.yaml`` 给定的私钥：

.. code-block:: yaml

    resources:
      - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
        tls_certificate:
          certificate_chain:
            filename: /certs/sds_cert.pem
          private_key:
            filename: /certs/sds_key.pem

验证 xDS 服务器证书的 CA 证书捆路径会在 SDS 配置文件 ``/etc/envoy/validation_context_sds_secret.yaml`` 中给出：

.. code-block:: yaml

    resources:
      - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
        validation_context:
          trusted_ca:
            filename: /certs/cacert.pem


统计：
------
SSL 套接字工厂输出遵循 SDS 相关统计。它们都是计数器类型。 

对于下游监听器，统计都在 *listener.<LISTENER_IP>.server_ssl_socket_factory.* 命名空间中。

.. csv-table::
     :header: Name, Description
     :widths: 1, 2

     ssl_context_update_by_sds, Total number of ssl context has been updated.
     downstream_context_secrets_not_ready, Total number of downstream connections reset due to empty ssl certificate.

对于上游集群，统计都在 *cluster.<CLUSTER_NAME>.client_ssl_socket_factory.* 命名空间中。

.. csv-table::
     :header: Name, Description
     :widths: 1, 2

     ssl_context_update_by_sds, Total number of ssl context has been updated.
     upstream_context_secrets_not_ready, Total number of upstream connections reset due to empty ssl certificate.
