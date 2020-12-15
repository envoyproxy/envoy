.. _config_thrift_filters_rate_limit:

限流
=====

* 全局限流 :ref:`架构概览 <arch_overview_global_rate_limit>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.ratelimit.v3.RateLimit>`
* 此过滤器的名称应该被配置为 *envoy.filters.thrift.rate_limit* 。

当请求的路由有一个或者多个与过滤器状态配置相匹配的 :ref:`限流配置 
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.RouteAction.rate_limits>` 时，Thrift 限流过滤器将会调用限流服务。一个请求可以应用于多个配置。每一个配置都会向限流服务发送一个描述符。

如果限流服务被调用，并且任何描述符的响应都会超过限制，就会返回一个显示内部错误的应用程序异常。

如果在调用限流服务时发生了错误，或者有错误返回，且 :ref:`failure_mode_deny
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.ratelimit.v3.RateLimit.failure_mode_deny>` 被设置为 true，则返回一个显示内部错误的应用程序异常。

.. _config_thrift_filters_rate_limit_stats:

统计
-----

过滤器的输出统计在 *cluster.<route target cluster>.ratelimit.* 命名空间中。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  ok, Counter, 来自限流服务的低于限制的响应总数。
  error, Counter, 访问限流服务的错误总数。
  over_limit, Counter, 来自限流服务的超限响应的总数。
  failure_mode_allowed, Counter, "由于 :ref:`failure_mode_deny
  <envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.ratelimit.v3.RateLimit.failure_mode_deny>` 被设置为 false，导致的被允许通过的错误请求总数。"
