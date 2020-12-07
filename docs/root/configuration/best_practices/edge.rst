.. _best_practices_edge:

将 Envoy 配置为边缘代理
=======================

Envoy 是一个生产就绪的边缘代理，然而，默认配置是为服务网格用例定制的，当需要将 Envoy 当作边缘代理使用时，一些值需要做一些调整。

TCP 代理应该做如下配置：

* 对于 admin 端点的限制访问，
* :ref:`overload_manager <config_overload_manager>` ，
* :ref:`监听器缓冲限制 <envoy_v3_api_field_config.listener.v3.Listener.per_connection_buffer_limit_bytes>` 的值为 32 KiB，
* :ref:`集群缓冲限制 <envoy_v3_api_field_config.cluster.v3.Cluster.per_connection_buffer_limit_bytes>` 的值为 32 KiB。

HTTP 代理还应做如下额外的配置：

* :ref:`use_remote_address <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.use_remote_address>` 的值为 true (为了避免消耗外部客户端的 HTTP 头部，详情可看 :ref:`HTTP header 消耗 <config_http_conn_man_header_sanitizing>` )，
* :ref:`连接和流超时  <faq_configuration_timeouts>` ，
* :ref:`HTTP/2 最大并发流限制 <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_concurrent_streams>` 的值为 100，
* :ref:`HTTP/2 初始流窗口大小限制 <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.initial_stream_window_size>` 的值为 64 KiB，
* :ref:`HTTP/2 初始连接窗口大小限制 <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.initial_connection_window_size>` 的值为 1 MiB。
* :ref:`headers_with_underscores_action 设置 <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.headers_with_underscores_action>` 的值为 REJECT_REQUEST，为了防止上游服务认为 '_' 和 '-' 是可互换的。
* :ref:`监听器连接限制 <config_listeners_runtime>` 。
* :ref:`全局下游连接限制 <config_overload_manager>` 。

下面内容是上述建议配置的一个 YAML 示例（摘自 :ref:`Google VRP <arch_overview_google_vrp>` 边缘服务器配置）：

.. literalinclude:: _include/edge.yaml
    :language: yaml
