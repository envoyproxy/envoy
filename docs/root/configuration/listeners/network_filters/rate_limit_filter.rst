.. _config_network_filters_rate_limit:

限流
==========

* 全局限流 :ref:`架构概览 <arch_overview_global_rate_limit>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.network.ratelimit.v3.RateLimit>`
* 过滤器的名称应该配置为 *envoy.filters.network.ratelimit* 。

.. note::
  本地限流是通过 :ref:`本地限流过滤器 <config_network_filters_local_rate_limit>` 来支持的。

.. _config_network_filters_rate_limit_stats:

统计
----------

每个限流过滤器的配置都有以 *ratelimit.<stat_prefix>.* 为根的统计信息，如下所示：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  total, Counter, 限流服务的请求总数
  error, Counter, 连接限流服务的总异常数
  over_limit, Counter, 限流服务的超限响应总数
  ok, Counter, 限流服务的非超限服务总数
  cx_closed, Counter, 由于限流服务而超限的关闭连接总数
  active, Gauge, 限流服务的活跃请求总数
  failure_mode_allowed, Counter, 由于 :ref:`failure_mode_deny <envoy_v3_api_field_extensions.filters.network.ratelimit.v3.RateLimit.failure_mode_deny>` 设置为 false 而允许通过的异常请求总数

运行时
-------

网络限流过滤器支持以下的运行时设置：

ratelimit.tcp_filter_enabled
  调用限流服务的连接百分比。默认为 100。

ratelimit.tcp_filter_enforcing
  调用限流服务并强制执行的连接百分比，默认为 100。这个可以被用于测试在强制执行之前会发生什么。
