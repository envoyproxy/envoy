.. _config_overview_management_server:

xDS API 端点
-----------------

xDS 管理服务器将根据 gRPC 和/或 REST 服务的要求实现以下端点。在 gRPC 流和 REST-JSON 两种情况下，都按照 :ref:`xDS 协议 <xds_protocol>` 发送 :ref:`DiscoveryRequest <envoy_v3_api_msg_service.discovery.v3.DiscoveryRequest>` 并接收 :ref:`DiscoveryResponse <envoy_v3_api_msg_service.discovery.v3.DiscoveryResponse>` 。

下面我们描述了 v2 和 v3 传输 API 版本的端点。

.. _v2_grpc_streaming_endpoints:

gRPC 流端点
^^^^^^^^^^^^^^^^^^^^^^^^

.. http:post:: /envoy.api.v2.ClusterDiscoveryService/StreamClusters
.. http:post:: /envoy.service.cluster.v3.ClusterDiscoveryService/StreamClusters

有关服务定义，请参见 :repo:`cds.proto <api/service/cluster/v3/cds.proto>` 。当以下内容

.. code-block:: yaml

    cds_config:
      api_config_source:
        api_type: GRPC
        transport_api_version: <V2|V3>
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

在 :ref:`Bootstrap <envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` 配置的 :ref:`dynamic_resources
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` 中设置时，Envoy 会将其用作客户端。

.. http:post:: /envoy.api.v2.EndpointDiscoveryService/StreamEndpoints
.. http:post:: /envoy.service.endpoint.v3.EndpointDiscoveryService/StreamEndpoints

有关服务定义，请参见 :repo:`eds.proto
<api/envoy/service/endpoint/v3/eds.proto>` 。当以下内容

.. code-block:: yaml

    eds_config:
      api_config_source:
        api_type: GRPC
        transport_api_version: <V2|V3>
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

在 :ref:`Cluster <envoy_v3_api_msg_config.cluster.v3.Cluster>` 配置的 :ref:`eds_cluster_config
<envoy_v3_api_field_config.cluster.v3.Cluster.eds_cluster_config>` 字段中设置时，Envoy 会将其用作客户端。

.. http:post:: /envoy.api.v2.ListenerDiscoveryService/StreamListeners
.. http:post:: /envoy.service.listener.v3.ListenerDiscoveryService/StreamListeners

有关服务定义，请参见 :repo:`lds.proto
<api/envoy/service/listener/v3/lds.proto>` 。当以下内容

.. code-block:: yaml

    lds_config:
      api_config_source:
        api_type: GRPC
        transport_api_version: <V2|V3>
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

在 :ref:`Bootstrap <envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` 配置的 :ref:`dynamic_resources
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` 中设置时，Envoy 会将其用作客户端。

.. http:post:: /envoy.api.v2.RouteDiscoveryService/StreamRoutes
.. http:post:: /envoy.service.route.v3.RouteDiscoveryService/StreamRoutes

有关服务定义，请参见 :repo:`rds.proto
<api/envoy/service/route/v3/rds.proto>` 。当以下内容

.. code-block:: yaml

    route_config_name: some_route_name
    config_source:
      api_config_source:
        api_type: GRPC
        transport_api_version: <V2|V3>
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

在 :ref:`HttpConnectionManager
<envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager>` 配置的 :ref:`rds
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.rds>` 字段中设置时，Envoy 会将其用作客户端。

.. http:post:: /envoy.api.v2.ScopedRoutesDiscoveryService/StreamScopedRoutes
.. http:post:: /envoy.service.route.v3.ScopedRoutesDiscoveryService/StreamScopedRoutes

有关服务定义，请参见 :repo:`srds.proto
<api/envoy/service/route/v3/srds.proto>` 。当以下内容

.. code-block:: yaml

    name: some_scoped_route_name
    scoped_rds:
      config_source:
        api_config_source:
          api_type: GRPC
          transport_api_version: <V2|V3>
          grpc_services:
            envoy_grpc:
              cluster_name: some_xds_cluster

在 :ref:`HttpConnectionManager
<envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager>` 配置的 :ref:`scoped_routes <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.scoped_routes>` 字段中设置时，Envoy 会将其用作客户端。

.. http:post:: /envoy.service.discovery.v2.SecretDiscoveryService/StreamSecrets
.. http:post:: /envoy.service.secret.v3.SecretDiscoveryService/StreamSecrets

有关服务定义，请参见 :repo:`sds.proto
<api/envoy/service/secret/v3/sds.proto>` 。当以下内容

.. code-block:: yaml

    name: some_secret_name
    config_source:
      api_config_source:
        api_type: GRPC
        transport_api_version: <V2|V3>
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

在 :ref:`SdsSecretConfig <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.SdsSecretConfig>` 消息中设置时， Envoy 会将其用作客户端。这种消息在各种地方都使用，如 :ref:`CommonTlsContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CommonTlsContext>` 。

.. http:post:: /envoy.service.discovery.v2.RuntimeDiscoveryService/StreamRuntime
.. http:post:: /envoy.service.runtime.v3.RuntimeDiscoveryService/StreamRuntime

有关服务定义，请参见 :repo:`rtds.proto
<api/envoy/service/runtime/v3/rtds.proto>` 。当以下内容

.. code-block:: yaml

    name: some_runtime_layer_name
    config_source:
      api_config_source:
        api_type: GRPC
        transport_api_version: <V2|V3>
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

在 :ref:`rtds_layer <envoy_v3_api_field_config.bootstrap.v3.RuntimeLayer.rtds_layer>`
字段中设置时，Envoy 会将其用作客户端。

REST 端点
^^^^^^^^^^^^^^

.. http:post:: /v2/discovery:clusters
.. http:post:: /v3/discovery:clusters

有关服务定义，请参见 :repo:`cds.proto
<api/envoy/service/cluster/v3/cds.proto>` 。当以下内容

.. code-block:: yaml

    cds_config:
      api_config_source:
        api_type: REST
        transport_api_version: <V2|V3>
        cluster_names: [some_xds_cluster]

在 :ref:`Bootstrap
<envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` 配置的 :ref:`dynamic_resources
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` 中设置时，Envoy 会将其用作客户端。

.. http:post:: /v2/discovery:endpoints
.. http:post:: /v3/discovery:endpoints

有关服务定义，请参见 :repo:`eds.proto
<api/envoy/service/endpoint/v3/eds.proto>` 。当以下内容

.. code-block:: yaml

    eds_config:
      api_config_source:
        api_type: REST
        transport_api_version: <V2|V3>
        cluster_names: [some_xds_cluster]

在 :ref:`Cluster
<envoy_v3_api_msg_config.cluster.v3.Cluster>` 配置的 :ref:`eds_cluster_config
<envoy_v3_api_field_config.cluster.v3.Cluster.eds_cluster_config>` 字段中设置时，Envoy 会将其用作客户端。

.. http:post:: /v2/discovery:listeners
.. http:post:: /v3/discovery:listeners

有关服务定义，请参见 :repo:`lds.proto
<api/envoy/service/listener/v3/lds.proto>` 。当以下内容

.. code-block:: yaml

    lds_config:
      api_config_source:
        api_type: REST
        transport_api_version: <V2|V3>
        cluster_names: [some_xds_cluster]

在 :ref:`Bootstrap
<envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` 配置的 :ref:`dynamic_resources
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` 中设置时，Envoy 会将其用作客户端。

.. http:post:: /v2/discovery:routes
.. http:post:: /v3/discovery:routes

有关服务定义，请参见 :repo:`rds.proto
<api/envoy/service/route/v3/rds.proto>` 。当以下内容

.. code-block:: yaml

    route_config_name: some_route_name
    config_source:
      api_config_source:
        api_type: REST
        transport_api_version: <V2|V3>
        cluster_names: [some_xds_cluster]

在 :ref:`HttpConnectionManager
<envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager>` 配置的 :ref:`rds
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.rds>` 字段中设置时，Envoy 会将其用作客户端。

.. note::

    响应这些端点的管理服务器必须以 :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`
    和 HTTP 状态 200 进行响应。此外，如果提供的配置未更改（如 Envoy 客户端提供的版本所示），则管理服务器可以响应具有空的正文，HTTP 状态为 304。

.. _config_overview_ads:

聚合发现服务
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

尽管 Envoy 从根本上采用了最终的一致性模型，但 ADS 提供了对 API 更新推送进行排序的机会，并确保单个管理服务器对 Envoy 节点的 API 更新具有亲和力。ADS 允许管理服务器在单个，双向 gRPC 流上交付一个或多个 API 及其资源。否则，某些 API（例如 RDS 和 EDS）可能需要管理多个流以及与不同管理服务器的连接。

ADS 将允许通过适当的顺序进行无中断的配置更新。例如，假设 *foo.com* 被映射到集群 *X*。我们希望在路由表来改变 *foo.com* 映射到集群 *Y*。为此，必须首先交付包含集群 *X* 和集群 *Y* 的 CDS/EDS 更新。

如果没有ADS，CDS/EDS/RDS 流可能指向不同的管理服务器，或者指向同一管理服务器上需要协调的不同 gRPC 流/连接。EDS 资源请求可以分为两个不同的流，一个用于 *X*，另一个用于 *Y*。ADS 允许将它们合并到单个管理服务器的单个流中，而无需进行分布式同步来正确地对更新进行排序。如果有 ADS，管理服务器将在单个流上交付 CDS，EDS 和 RDS 的更新。

ADS 仅可用于 gRPC 流式传输（不适用于 REST），并且在 :ref:`xDS <xds_protocol_ads>` 文档中有更完整的描述。gRPC 端点是：

.. http:post:: /envoy.service.discovery.v2.AggregatedDiscoveryService/StreamAggregatedResources
.. http:post:: /envoy.service.discovery.v3.AggregatedDiscoveryService/StreamAggregatedResources

有关服务定义，请参见 :repo:`discovery.proto
<api/envoy/service/discovery/v3/discovery.proto>` 。当以下内容

.. code-block:: yaml

    ads_config:
      api_type: GRPC
      transport_api_version: <V2|V3>
      grpc_services:
        envoy_grpc:
          cluster_name: some_ads_cluster

在 :ref:`Bootstrap
<envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` 配置的 :ref:`dynamic_resources
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` 中设置时，Envoy 会将其用作客户端。

设置此选项后，可以将 :ref:`以上 <v2_grpc_streaming_endpoints>` 任何配置源设置为使用 ADS 通道。例如，可以将 LDS 配置从

.. code-block:: yaml

    lds_config:
      api_config_source:
        api_type: REST
        cluster_names: [some_xds_cluster]

改为

.. code-block:: yaml

    lds_config: {ads: {}}

结果是 LDS 流将通过共享的 ADS 通道定向到 *some_ads_cluster*。

.. _config_overview_delta:

Delta 端点
^^^^^^^^^^^^^^^

REST，文件系统和原始 gRPC xDS 实现都提供全量更新：每个 CDS 更新都必须包含每个集群，更新中没有集群意味着集群已消失。对于具有大量资源甚至是少量流失的 Envoy 部署，这些最新状态的更新可能很麻烦。

从 1.12.0 版开始, Envoy 支持 xDS（包括 ADS）的“delta”变体, 其中更新仅包含添加/更改/删除的资源。Delta xDS 是 gRPC (仅) 协议。Delta 使用与 SotW（DeltaDiscovery {Request，Response}）不同的请求/响应 proto；请参阅 :repo:`discovery.proto <api/envoy/service/discovery/v3/discovery.proto>` 。从概念上讲，应将 delta 视为一种新的 xDS 传输类型：存在静态，文件系统，REST，gRPC-SotW 和现在的 gRPC-delta。（Envoy 的 gRPC-SotW/delta 客户端实现恰好在两者之间共享了大部分代码，并且在服务器端可能有类似的可能。但是，它们实际上是不兼容的协议。:ref:`delta xDS 协议行为的规范在这里 <xds_protocol_delta>`。）

要使用 delta，只需将你 :ref:`ApiConfigSource <envoy_v3_api_msg_config.core.v3.ApiConfigSource>` 上原始 api_type 字段设置为 DELTA_GRPC。这对 xDS 和 ADS 都适用；对于 ADS，它是 :ref:`DynamicResources.ads_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` 的 api_type 字段 ，如上一节所述。