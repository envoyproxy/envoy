.. _config_overview_extension_configuration:

扩展配置
----------

Envoy 中的每一个配置资源，在 `typed_config` 中都有一个类型 URL。此类型对应于一个带版本的 schema。如果一个类型 URL 能够唯一标识解释性配置的可扩展能力，则此扩展会被选择，而不会去考虑  `name` 字段。在此情况下，`name` 字段是可选的，可以被当作一个标识符或可扩展配置特定实例的注解来使用。比如，允许如下所示的的过滤器配置片段：

.. code-block:: yaml

  name: front-http-proxy
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
            envoy_grpc:
              cluster_name: xds_cluster
    http_filters:
    - name: front-router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
        dynamic_stats: true


以防控制面缺乏对扩展的 schema 定义能力，`udpa.type.v1.TypedStruct` 应该被用来作为一个通用容器。容器内的类型 URL 就可以被客户端用来将内容转换为特定类型的配置资源。比如，上述示例也可以写为如下所示的例子：

.. code-block:: yaml

  name: front-http-proxy
  typed_config:
    "@type": type.googleapis.com/udpa.type.v1.TypedStruct
    type_url: type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    value:
      stat_prefix: ingress_http
      codec_type: AUTO
      rds:
        route_config_name: local_route
        config_source:
          api_config_source:
            api_type: GRPC
            grpc_services:
              envoy_grpc:
                cluster_name: xds_cluster
      http_filters:
      - name: front-router
        typed_config:
          "@type": type.googleapis.com/udpa.type.v1.TypedStruct
          type_url: type.googleapis.com/envoy.extensions.filters.http.router.v3Router

.. _config_overview_extension_discovery:

发现服务
^^^^^^^^^^^

扩展配置可以使用 :ref:`ExtensionConfiguration 发现服务 <envoy_v3_api_file_envoy/service/extension/v3/config_discovery.proto>` 的 :ref:`xDS 管理服务器 <xds_protocol>` 来动态提供。扩展配置中的 name 字段充当资源标识符。比如，HTTP 连接管理器支持 HTTP 过滤器的 :ref:`动态过滤器重配置 <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.config_discovery>`。

Extension config discovery service has a :ref:`statistics
<subscription_statistics>` tree rooted at
*<stat_prefix>.extension_config_discovery.<extension_config_name>*. In addition
to the common subscription statistics, it also provides the following:

扩展配置发现服务有一个以 *<stat_prefix>.extension_config_discovery.<extension_config_name>* 为根的 :ref:`统计 <subscription_statistics>` 树。除了公共订阅统计，它还提供如下统计：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  config_reload, Counter, 配置更新成功的总数
  config_fail, Counter, 配置更新失败的总数
