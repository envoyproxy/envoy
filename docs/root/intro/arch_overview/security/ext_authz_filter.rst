.. _arch_overview_ext_authz:

外部授权
======================

* :ref:`网络层过滤器配置 <config_network_filters_ext_authz>`。
* :ref:`HTTP 过滤器配置 <config_http_filters_ext_authz>`。

外部授权过滤器调用授权服务以检查传入请求是否被授权。过滤器可以配置为 :ref:`网络层过滤器 <config_network_filters_ext_authz>` 或 :ref:`HTTP 过滤器 <config_http_filters_ext_authz>` 或同时配置两者。如果该请求被网络层过滤器视为未被授权，则连接将被关闭。如果该请求在 HTTP 过滤器中被视为未被授权，则该请求将被 403（禁止）响应拒绝。

.. tip::
  建议将授权过滤器配置为过滤器链中的第一个过滤器，以便在其余过滤器处理请求之前对请求进行授权处理。

外部授权服务群集可以是静态配置的，也可以是通过 :ref:`集群服务发现 <config_cluster_manager_cds>` 配置的。如果在请求到达时外部服务不可用，则该请求是否被授权由 :ref:`网络层过滤器 <envoy_v3_api_msg_extensions.filters.network.ext_authz.v3.ExtAuthz>` 或 :ref:`HTTP 过滤器 <envoy_v3_api_msg_extensions.filters.http.ext_authz.v3.ExtAuthz>` 中的 *failure_mode_allow* 配置项的设置决定。如果将其设置为 true，则该请求将被放行（故障打开），否则将被拒绝。
默认设置为 false。

服务定义
------------------

与外部授权服务通信的上下文使用此处的定义
传递给授权服务的请求内容由 :ref:`CheckRequest <envoy_v3_api_msg_service.auth.v3.CheckRequest>` 指定。

.. toctree::
  :glob:
  :maxdepth: 2

  ../../../api-v3/service/auth/v3/*
