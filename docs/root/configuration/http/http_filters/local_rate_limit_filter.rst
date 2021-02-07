.. _config_http_filters_local_rate_limit:

本地限流
================

* 本地限流 :ref:`架构概览 <arch_overview_local_rate_limit>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>`
* 此过滤器应使用 *envoy.filters.http.local_ratelimit* 名称进行配置。

当请求的路由或者虚拟主机各自具有过滤器的 :ref:`本地限流配置 <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>` 时，HTTP 本地限流过滤器将应用 :ref:`令牌桶 <envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.token_bucket>` 限流。

如果本地限流令牌桶经过检查且没有可用的令牌，则返回 429 响应（响应是可配置的）。本地限流过滤器同时会设置 :ref:`x-envoy-ratelimited<config_http_filters_router_x-envoy-ratelimited>` 头部。可配置额外的响应头部。

示例配置
---------------------

全局设置限流器的过滤器示例配置（例如：所有虚拟主机/路由共享同一个令牌桶)：

.. code-block:: yaml

  name: envoy.filters.http.local_ratelimit
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
    stat_prefix: http_local_rate_limiter
    token_bucket:
      max_tokens: 10000
      tokens_per_fill: 1000
      fill_interval: 1s
    filter_enabled:
      runtime_key: local_rate_limit_enabled
      default_value:
        numerator: 100
        denominator: HUNDRED
    filter_enforced:
      runtime_key: local_rate_limit_enforced
      default_value:
        numerator: 100
        denominator: HUNDRED
    response_headers_to_add:
      - append: false
        header:
          key: x-local-rate-limit
          value: 'true'


全局禁用限流器但为特定路由启用的过滤器示例配置：

.. code-block:: yaml

  name: envoy.filters.http.local_ratelimit
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
    stat_prefix: http_local_rate_limiter


特定路由的配置：

.. code-block:: yaml

  route_config:
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match: { prefix: "/path/with/rate/limit" }
        route: { cluster: service_protected_by_rate_limit }
        typed_per_filter_config:
          envoy.filters.http.local_ratelimit:
            "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
            token_bucket:
              max_tokens: 10000
              tokens_per_fill: 1000
              fill_interval: 1s
            filter_enabled:
              runtime_key: local_rate_limit_enabled
              default_value:
                numerator: 100
                denominator: HUNDRED
            filter_enforced:
              runtime_key: local_rate_limit_enforced
              default_value:
                numerator: 100
                denominator: HUNDRED
            response_headers_to_add:
              - append: false
                header:
                  key: x-local-rate-limit
                  value: 'true'
      - match: { prefix: "/" }
        route: { cluster: default_service }


需要注意的是如果此过滤器已经配置为全局禁用并且没有虚拟主机或路由级别的令牌桶，则限流不会生效。

统计
----------

本地限流过滤器在 *<stat_prefix>.http_local_rate_limit.* 命名空间中输出统计信息。429 响应 -- 或者已经配置的状态码 -- 会提交到常规集群 :ref:`动态 HTTP 统计信息 <config_cluster_manager_cluster_stats_dynamic_http>` 中。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  enabled, Counter, 与限流过滤器协商过的请求总数
  ok, Counter, 低于令牌桶限制的响应总数
  rate_limited, Counter, 没有可用令牌的响应总数（但不一定强制执行）
  enforced, Counter, 限流所作用到的请求总数（例如：429 返回）

.. _config_http_filters_local_rate_limit_runtime:

运行时
-------

HTTP 限流过滤器支持如下的运行时部分设置：

http_filter_enabled
  在 :ref:`本地限流配置 <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>` 中指定了 *route_key* 后，将会检查本地限流决策但不强制执行的请求百分比。默认值为 0。

http_filter_enforcing
  在 :ref:`本地限流配置 <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>` 中指定了 *route_key* 后，将会强制执行本地限流决策的请求百分比。默认值为 0。这可以用于测试在完全执行结果之前会发生什么。
