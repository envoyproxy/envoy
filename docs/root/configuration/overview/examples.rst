示例
--------

下面是我们将使用 YAML 表示的配置原型，以及从 127.0.0.1:10000 到 127.0.0.2:1234 的服务代理 HTTP 的运行示例。

静态
^^^^^^

下面提供了一个最小的完全静态 bootstrap 配置：

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address: { address: 127.0.0.1, port_value: 9901 }

  static_resources:
    listeners:
    - name: listener_0
      address:
        socket_address: { address: 127.0.0.1, port_value: 10000 }
      filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: ingress_http
            codec_type: AUTO
            route_config:
              name: local_route
              virtual_hosts:
              - name: local_service
                domains: ["*"]
                routes:
                - match: { prefix: "/" }
                  route: { cluster: some_service }
            http_filters:
            - name: envoy.filters.http.router
    clusters:
    - name: some_service
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: some_service
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 1234

大多是拥有动态 EDS 的静态配置
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

下面提供了一个 bootstrap 配置，该配置从以上示例开始，通过监听 127.0.0.3:5678 的 :ref:`EDS <envoy_v3_api_file_envoy/service/endpoint/v3/eds.proto>` gRPC 管理服务器进行 :ref:`动态端点发现 <arch_overview_dynamic_config_eds>`：

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address: { address: 127.0.0.1, port_value: 9901 }

  static_resources:
    listeners:
    - name: listener_0
      address:
        socket_address: { address: 127.0.0.1, port_value: 10000 }
      filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: ingress_http
            codec_type: AUTO
            route_config:
              name: local_route
              virtual_hosts:
              - name: local_service
                domains: ["*"]
                routes:
                - match: { prefix: "/" }
                  route: { cluster: some_service }
            http_filters:
            - name: envoy.filters.http.router
    clusters:
    - name: some_service
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      type: EDS
      eds_cluster_config:
        eds_config:
          api_config_source:
            api_type: GRPC
            grpc_services:
              - envoy_grpc:
                  cluster_name: xds_cluster
    - name: xds_cluster
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      http2_protocol_options:
        connection_keepalive:
          interval: 30s
          timeout: 5s
      upstream_connection_options:
        # configure a TCP keep-alive to detect and reconnect to the admin
        # server in the event of a TCP socket half open connection
        tcp_keepalive: {}
      load_assignment:
        cluster_name: xds_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 5678

注意上面 *xds_cluster* 被定义为指向 Envoy 管理服务器。即使在完全动态的配置中，也需要定义一些静态资源，从而将 Envoy 指向其 xDS 管理服务器。

在 `tcp_keepalive` 块中设置适当的 :ref:`TCP Keep-Alive 选项 <envoy_v3_api_msg_config.core.v3.TcpKeepalive>` 非常重要。这将有助于检测与 xDS 管理服务器的 TCP 半开连接，并重新建立完整连接。

在上面的例子中，EDS 管理服务器可以返回一个 proto 编码的 :ref:`DiscoveryResponse <envoy_v3_api_msg_service.discovery.v3.DiscoveryResponse>`：

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment
    cluster_name: some_service
    endpoints:
    - lb_endpoints:
      - endpoint:
          address:
            socket_address:
              address: 127.0.0.2
              port_value: 1234


上面显示的版本控制和类型 URL 方案在 :ref:`流式 gRPC 订阅协议 <xds_protocol_streaming_grpc_subscriptions>` 文档中有更详细的解释。

动态
^^^^^^^

下面提供了完全动态的 bootstrap 配置，其中除了属于管理服务器资源外的所有资源都是通过 xDS 发现的：

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address: { address: 127.0.0.1, port_value: 9901 }

  dynamic_resources:
    lds_config:
      api_config_source:
        api_type: GRPC
        grpc_services:
          - envoy_grpc:
              cluster_name: xds_cluster
    cds_config:
      api_config_source:
        api_type: GRPC
        grpc_services:
          - envoy_grpc:
              cluster_name: xds_cluster

  static_resources:
    clusters:
    - name: xds_cluster
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      http2_protocol_options:
        # Configure an HTTP/2 keep-alive to detect connection issues and reconnect
        # to the admin server if the connection is no longer responsive.
        connection_keepalive:
          interval: 30s
          timeout: 5s
      load_assignment:
        cluster_name: xds_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 5678

管理服务器将如下响应 LDS 请求：

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.config.listener.v3.Listener
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 10000
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: ingress_http
          codec_type: AUTO
          rds:
            route_config_name: local_route
            config_source:
              api_config_source:
                api_type: GRPC
                grpc_services:
                  - envoy_grpc:
                      cluster_name: xds_cluster
          http_filters:
          - name: envoy.filters.http.router

管理服务将如下响应 RDS 请求：

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match: { prefix: "/" }
        route: { cluster: some_service }

管理服务器将如下响应 CDS 请求：

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
    name: some_service
    connect_timeout: 0.25s
    lb_policy: ROUND_ROBIN
    type: EDS
    eds_cluster_config:
      eds_config:
        api_config_source:
          api_type: GRPC
          grpc_services:
            - envoy_grpc:
                cluster_name: xds_cluster

管理服务器将如下响应 EDS 请求：

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment
    cluster_name: some_service
    endpoints:
    - lb_endpoints:
      - endpoint:
          address:
            socket_address:
              address: 127.0.0.2
              port_value: 1234