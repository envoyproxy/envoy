.. _config_http_filters_rate_limit:

限流
==========

* 全局限流 :ref:`架构概览 <arch_overview_global_rate_limit>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.ratelimit.v3.RateLimit>`
* 此过滤器应使用 *envoy.filters.http.ratelimit* 名称进行配置。

当请求的路由或者虚拟主机有一个或者多个 :ref:`限流配置<envoy_v3_api_field_config.route.v3.VirtualHost.rate_limits>` 匹配到过滤级设置时，HTTP 限流过滤器将会调用限流服务。:ref:`路由<envoy_v3_api_field_config.route.v3.RouteAction.include_vh_rate_limits>` 可以选择包括虚拟主机的限流配置。多项配置可以同时应用到一个请求上。每项配置都会使描述符被发送到限流服务上。

如果限流服务被调用，并且有任何的描述符响应超出限制，则返回 429 响应。限流过滤器还会为响应设置 :ref:`x-envoy-ratelimited<config_http_filters_router_x-envoy-ratelimited>` 头部。

如果调用限流服务时发生错误，或者限流服务返回错误，并且 :ref:`failure_mode_deny <envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.failure_mode_deny>` 的设置为 true，则返回 500 响应。

.. _config_http_filters_rate_limit_composing_actions:

组合动作
-----------------

路由或虚拟主机上的每个 :ref:`限流动作 <envoy_v3_api_msg_config.route.v3.RateLimit>` 都会填充一个描述符条目。描述符条目的向量组成描述符。要创建更复杂的限流描述符，可以按任意顺序组合动作。描述符将按照配置中指定动作的顺序进行填充。

示例 1
^^^^^^^^^

例如，要生成如下的描述符：

.. code-block:: cpp

  ("generic_key", "some_value")
  ("source_cluster", "from_cluster")

配置将会是：

.. code-block:: yaml

  actions:
      - {source_cluster: {}}
      - {generic_key: {descriptor_value: some_value}}

示例 2
^^^^^^^^^

如果动作中没有附加描述符条目，则配置中不会生成描述符。

对于以下配置：

.. code-block:: yaml

  actions:
      - {source_cluster: {}}
      - {remote_address: {}}
      - {generic_key: {descriptor_value: some_value}}


如果请求未设置 :ref:`x-forwarded-for<config_http_conn_man_headers_x-forwarded-for>`，则不会生成描述符。

如果请求设置了 :ref:`x-forwarded-for<config_http_conn_man_headers_x-forwarded-for>`，则会生成如下的描述符：

.. code-block:: cpp

  ("generic_key", "some_value")
  ("remote_address", "<trusted address from x-forwarded-for>")
  ("source_cluster", "from_cluster")

.. _config_http_filters_rate_limit_rate_limit_override:

限流覆盖
-------------------

:ref:`限流动作 <envoy_v3_api_msg_config.route.v3.RateLimit>` 可以选择包含 :ref:`限流覆盖 <envoy_v3_api_msg_config.route.v3.RateLimit.Override>`。限制值将附加到动作产生的描述符中，并发送到限流服务，从而覆盖静态服务配置。

可以将覆盖配置为从指定的 :ref:`动态元数据 <envoy_v3_api_msg_config.core.v3.Metadata>` 下的 :ref: `键 <envoy_v3_api_msg_config.type.metadata.v3.MetadataKey>` 中获取。如果该值配置错误或者键不存在，则覆盖配置将会被忽略。

示例 3
^^^^^^^^^

如下配置

.. code-block:: yaml

  actions:
      - {generic_key: {descriptor_value: some_value}}
  limit:
     metadata_key:
         key: test.filter.key
         path:
             - key: test

.. _config_http_filters_rate_limit_override_dynamic_metadata:

将查找动态元数据的值。这个值必须是有整数字段 “requests_per_unit” 和字符串字段 “unit”，可以解析为 :ref:`RateLimitUnit 枚举 <envoy_v3_api_enum_type.v3.RateLimitUnit>` 的结构。例如，使用如下动态元数据，则每小时限制 42 个请求的限流覆盖将被附加到限流描述符中。

.. code-block:: yaml

  test.filter.key:
      test:
          requests_per_unit: 42
          unit: HOUR

统计
----------

限流过滤器会输出 *cluster.<route target cluster>.ratelimit.* 命名空间下的统计信息。429 响应会被发送到常规集群 :ref:`动态 HTTP 统计信息 <config_cluster_manager_cluster_stats_dynamic_http>` 中。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  ok, Counter, 处于限流服务限制之下的响应总数
  error, Counter, 与限流服务相关的错误总数
  over_limit, Counter, 超出限流服务限制的响应总数
  failure_mode_allowed, Counter, "出错但由于 :ref:`failure_mode_deny <envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.failure_mode_deny>` 设置为 false 而被允许通过的请求总数。"

运行时
-------

HTTP 限流过滤器支持如下的运行时设置：

ratelimit.http_filter_enabled
  将调用限流服务的请求百分比。默认值为 100。

ratelimit.http_filter_enforcing
  将强制决定执行限流服务的请求百分比。默认值为 100。这可以用来在完全执行结果之前测试会发生什么。

ratelimit.<route_key>.http_filter_enabled
  在 :ref:`限流配置 <envoy_v3_api_msg_config.route.v3.RateLimit>` 中指定了 *route_key* 的情况下，将会调用限流服务的请求百分比。默认值为 100。
