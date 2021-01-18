.. _config_http_conn_man_vhds:

虚拟主机发现服务（VHDS）
=====================================

虚拟主机发现服务（VHDS）API 是 Envoy 用来动态获取 :ref:`虚拟主机 <envoy_v3_api_msg_config.route.v3.VirtualHost>` 的可选 API。虚拟主机包括名称和域名的集合，这些名称和域名的集合根据传入请求的主机头部路由到该主机。

在 RDS 中，默认情况下，所有指向集群的路由都会分发给网格中的每个 Envoy 实例。随着群集规模的增长，这会导致扩展问题。这种复杂性的大部分可以在虚拟主机配置中找到，其中绝大部分虚拟主机配置对单个代理来说都是无用的。

为了解决此问题，虚拟主机发现服务（VHDS）协议使用增量 xDS 协议来允许订阅路由配置，并根据需要请求必要的虚拟主机。使用 VHDS 允许 Envoy 实例订阅和取消订阅内部存储在 xDS 管理服务器中的虚拟主机列表，而不是通过路由配置发送所有虚拟主机。xDS 管理服务器将监视此列表，并使用它过滤发送到单个 Envoy 实例的配置，使其仅包含已订阅的虚拟主机。

虚拟主机资源命名约定
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
VHDS 中的虚拟主机由虚拟主机所属的路由配置的名称以及 HTTP *host* 头部（HTTP2 的：*:authority* ）的组合来标识。资源应命名如下：

<route configuration name>/<host entry>

请注意，匹配应该从右到左进行，因为主机条目不能包含斜线，而路由配置名称可以。

订阅资源
^^^^^^^^^^^^^^^^^^^^^^^^
VHDS 允许使用 :ref:`DeltaDiscoveryRequest <envoy_v3_api_msg_service.discovery.v3.DeltaDiscoveryRequest>` 来 :ref:`订阅 <xds_protocol_delta_subscribe>` 资源，其 :ref:`type_url <envoy_v3_api_field_service.discovery.v3.DeltaDiscoveryRequest.type_url>` 设置为 `type.googleapis.com/envoy.config.route.v3.VirtualHost`， :ref:`resource_names_subscribe <envoy_v3_api_field_service.discovery.v3.DeltaDiscoveryRequest.resource_names_subscribe>` 设置为其配置的虚拟主机资源名称列表。

如果无法解析 host/authority 头部内容的路由，则在发送 :ref:`DeltaDiscoveryRequest <envoy_v3_api_msg_service.discovery.v3.DeltaDiscoveryRequest>` 时暂停活动流。当 :ref:`DeltaDiscoveryResponse <envoy_v3_api_msg_service.discovery.v3.DeltaDiscoveryResponse>` 被收到的时候，并且该 :ref:`DeltaDiscoveryResponse <envoy_v3_api_msg_service.discovery.v3.DeltaDiscoveryResponse>` 其中的一个 :ref:`别名 <envoy_v3_api_field_service.discovery.v3.Resource.aliases>` 或 :ref:`名称 <envoy_v3_api_field_service.discovery.v3.Resource.name>` 完全匹配 :ref:`DeltaDiscoveryRequest <envoy_v3_api_msg_service.discovery.v3.DeltaDiscoveryRequest>` 中的 :ref:`resource_names_subscribe <envoy_v3_api_field_service.discovery.v3.DeltaDiscoveryRequest.resource_names_subscribe>` ，路由配置被更新时，该流被恢复，并且所述过滤器链的处理继续。

对虚拟主机的更新以两种方式发生。如果虚拟主机最初是通过 RDS 发送的，则应通过 RDS 更新虚拟主机。如果通过 VHDS 订阅了虚拟主机，则更新将通过 VHDS 进行。

更新路由配置条目时，如果 :ref:`vhds 字段 <envoy_v3_api_field_config.route.v3.RouteConfiguration.vhds>` 已更改，则将清除该路由配置的虚拟主机列表，并且要求再次发送所有虚拟主机列表。

与作用域 RDS 的兼容性
-----------------------------

VHDS 不应该与 :ref:`作用域 RDS <envoy_v3_api_msg_config.route.v3.ScopedRouteConfiguration>` 出现任何兼容性问题 。路由配置名称仍可以用于虚拟主机匹配，但是在配置了作用域 RDS 的情况下，它将指向作用域路由配置。

但是，必须注意，按需 :ref:`作用域 RDS <envoy_v3_api_msg_config.route.v3.ScopedRouteConfiguration>` 和 VHDS 一起使用时，每个路由作用域需要两个按需订阅。

* :ref:`v2 API 参考 <v2_grpc_streaming_endpoints>`

统计
----------

VHDS 有一个统计树，其根目录为 *http.<stat_prefix>.vhds.<virtual_host_name>.*.。``virtual_host_name`` 中的所有 ``:`` 字符都将在统计树中被 ``_`` 替换。统计树包含以下统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  config_reload, Counter, 由于配置不同而导致重新加载配置的总 API 获取次数
  empty_update, Counter,  收到的空更新总数
