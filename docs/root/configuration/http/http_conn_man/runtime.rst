.. _config_http_conn_man_runtime:

运行时
=======

HTTP 连接管理器支持以下运行时设置：

.. _config_http_conn_man_runtime_normalize_path:

http_connection_manager.normalize_path
  如果在 :ref:`normalize_path <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.normalize_path>` 中未做配置时，采用路径规范的请求百分比。这在配置加载时进行评估，且会将给定配置应用于所有请求。

.. _config_http_conn_man_runtime_client_enabled:

tracing.client_enabled
  如果设置了 :ref:`config_http_conn_man_headers_x-client-trace-id` 头部，被强制追踪的请求百分比。默认值是 100。

.. _config_http_conn_man_runtime_global_enabled:

tracing.global_enabled
  在采用所有其他检查（强制追踪、采样等）后，被追踪的请求百分比。默认值是 100。

.. _config_http_conn_man_runtime_random_sampling:

tracing.random_sampling
  被随机追踪的请求百分比。更多信息，查看 :ref:`这儿 <arch_overview_tracing>`。此运行时控制被设定在 0-10000 之间。默认值是 10000。因此，追踪采样可以以 0.01% 的增量来指定。
