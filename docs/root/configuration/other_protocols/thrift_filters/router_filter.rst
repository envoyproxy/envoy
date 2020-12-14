.. _config_thrift_filters_router:

路由
=====

路由过滤器实现了 Thrift 转发。几乎所有的 Thrift 代理场景都会用到它。过滤器的主要工作就是遵从配置在
:ref:`路由表 <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.v3.RouteConfiguration>` 中指定的指令。

* :ref:`v3 API 参考 <envoy_v3_api_msg_config.filter.thrift.router.v2alpha1.Router>`
* 此过滤器的名称应该被配置为 *envoy.filters.thrift.router*。

统计
----

过滤器输出的统计信息都在 *thrift.<stat_prefix>.* 命名空间内。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  route_missing, Counter, 未找到路由的请求总数。
  unknown_cluster, Counter, 具有未知集群路由的请求总数。
  upstream_rq_maintenance_mode, Counter, 目标集群处于维护模式下的请求总数。
  no_healthy_upstream, Counter, 没有健康的上游端点可用的请求总数。
