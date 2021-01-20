.. _config_http_filters_grpc_stats:

gRPC 统计
===============

* gRPC :ref:`架构概览 <arch_overview_grpc>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.grpc_stats.v3.FilterConfig>`
* 该过滤器的名称应该配置为 *envoy.filters.http.grpc_stats* 。
* 这个过滤器可以被启用来发送 :ref:`过滤器状态对象
  <envoy_v3_api_msg_extensions.filters.http.grpc_stats.v3.FilterObject>`

这是一个启用了 gRPC 调用遥测的过滤器。此外，该过滤器还可以检测 gRPC 流调用中的消息起始，并发出请求和响应的消息计数。

更多信息：连接协议格式参考 `gRPC over HTTP/2 <https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md>`_。

过滤器在 *cluster.<route target cluster>.grpc.* 命名空间中发出统计信息。 取决于该配置，统计信息可以以 `<grpc service>.<grpc method>.` 为前缀；表中的统计信息以这种形式显示。请参阅有关的文档 :ref:`individual_method_stats_allowlist <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.individual_method_stats_allowlist>` 和 :ref:`stats_for_all_methods <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.stats_for_all_methods>`。

启用 *upstream_rq_time* (只有 v3 API 包含) 参考 :ref:`enable_upstream_stats <envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.enable_upstream_stats>`。


.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  <grpc service>.<grpc method>.success, Counter, 成功的服务/方法调用总数
  <grpc service>.<grpc method>.failure, Counter, 失败的服务/方法调用总数
  <grpc service>.<grpc method>.total, Counter, 服务/方法调用总数
  <grpc service>.<grpc method>.request_message_count, Counter, 服务/方法调用的请求消息总数
  <grpc service>.<grpc method>.response_message_count, Counter, 服务/方法调用的响应消息总数
  <grpc service>.<grpc method>.upstream_rq_time, Histogram, 请求时间毫秒
