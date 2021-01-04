.. _config_http_conn_man_rds:

路由发现服务（RDS）
====================

路由发现服务（RDS）API 是一个可选 API，Envoy 用来动态获取 :ref:`路由配置 <envoy_v3_api_msg_config.route.v3.RouteConfiguration>`。路由配置同时包含 HTTP 头部修改、虚拟主机以及包含于每个虚拟主机中的单个路由入口。每一个 :ref:`HTTP 连接管理器过滤器 <config_http_conn_man>` 都可以通过 API 来独立地获取它自己的路由配置。根据需要，:ref:`虚拟主机发现服务 
<config_http_conn_man_vhds>` 可用于从路由配置中分别获取虚拟主机。

* :ref:`v2 API 参考 <v2_grpc_streaming_endpoints>`

统计
------

RDS 有一个以  *http.<stat_prefix>.rds.<route_config_name>.* 为根的 :ref:`统计 <subscription_statistics>` 树。
在统计树里，``route_config_name`` 名称中的任何 ``:`` 都会被替换为 ``_``。
