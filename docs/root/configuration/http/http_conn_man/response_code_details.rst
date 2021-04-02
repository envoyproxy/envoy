.. _config_http_conn_man_details:

响应代码详细信息
=====================

如果通过 :ref:`访问日志<config_access_log_format_response_code_details>`，或
:ref:`自定义头部<config_http_conn_man_headers_custom_request_headers>` 配置了 _%RESPONSE_CODE_DETAILS_，
Envoy 将传达一个给定流结束的详细原因。此页列出了 HttpConnectionManager、路由器过滤器和编解码器发送的详细信息。
但这是不全面的，因为其他任何过滤器都可能会发送带有自定义详细信息的本地答复。

下面列出了 HttpConnectionManager 或路由器过滤器可能会发送的响应或重置流的原因。

.. warning::
  以下列表不能保证是稳定的，因为细节可能会发生变化。

.. csv-table::
   :header: 名称, 描述
   :widths: 1, 2

   absolute_path_rejected, 由于在不支持绝对路径的路由上使用绝对路径，请求被拒绝。
   admin_filter_response, 响应由管理过滤器生成。
   cluster_not_found, 由于找不到所选路由的集群，路由器过滤器拒绝了请求。
   downstream_local_disconnect, 由于未指定的原因，客户端连接在本地关闭。
   downstream_remote_disconnect, 客户端意外断开连接。
   duration_timeout, 已经超过最大连接持续时间。
   direct_response, 路由器过滤器生成直接响应。
   filter_chain_not_found, 由于没有匹配的过滤器链，请求被拒绝。
   internal_redirect, 原始流被内部重定向替换。
   low_version, 由于没有配置对 HTTP/1.0 的支持，HTTP/1.0 或 HTTP/0.9 的请求被拒绝。
   maintenance_mode, 由于集群于维护模式，请求被路由器过滤器拒绝。
   max_duration_timeout, 已超过每个流的最大持续超时时间。
   missing_host_header, 由于缺少 Host: 或 :authority 字段，请求被拒绝。
   missing_path_rejected, 由于缺少路径或 :path 头部字段，请求被拒绝。
   no_healthy_upstream, 由于找不到健康的上游，路由器过滤器拒绝了请求。
   overload, 由于过载管理器达到配置的资源限制，请求被拒绝。
   path_normalization_failed, 由于配置了路径规范化，但失败了，请求被拒绝，可能是因为路径无效。
   request_headers_failed_strict_check, 由于 x-envoy-* 头部验证失败，请求被拒绝。
   request_overall_timeout, 已超过每个流的总请求超时时间。
   request_payload_exceeded_retry_buffer_limit, Envoy 正在执行流代理，但在等待重试时，到达的数据太多。
   request_payload_too_large, Envoy 正在执行非流代理，请求负载超出了配置的限制。
   response_payload_too_large, Envoy 正在执行非流代理，响应的负载超出了配置的限制。
   route_configuration_not_found, 由于找不到路由配置，请求被拒绝。
   route_not_found, 由于找不到路由，请求被拒绝。
   stream_idle_timeout, 已超出每个流的 keepalive 超时时间。
   upgrade_failed, 由于请求一个不受支持的升级，请求被拒绝。
   upstream_max_stream_duration_reached, 由于请求超出了配置的最大流持续时间，请求被销毁。
   upstream_per_try_timeout, 最后一次上游尝试失败。
   upstream_reset_after_response_started{details}, 响应启动后，上游连接被重置。可能包括有关断开原因的更多详细信息。
   upstream_reset_before_response_started{details}, 响应启动前，上游连接被重置。可能包括有关断开原因的更多详细信息。
   upstream_response_timeout, 上游响应超时。
   via_upstream, 响应代码由上游服务器设置。


.. _config_http_conn_man_details_per_codec:

每个编解码器的详细信息
-----------------------

遇到错误时，每个编解码器都可能会发送编解码器特定的详细信息。

Http1 详细信息
~~~~~~~~~~~~~~~~

所有 http1 的详细信息都以 *http1.* 为根。

.. csv-table::
   :header: 名称, 描述
   :widths: 1, 2

   http1.body_disallowed, 在不允许请求体的请求下发送了请求体。
   http1.codec_error, http_parser 内部遇到了一些错误。
   http1.connection_header_rejected, 连接头部格式不正确或者过长。
   http1.content_length_and_chunked_not_allowed, 在配置不允许的情况下，使用 Transfer-Encoding: 和 Content-Length 头部发送请求。
   http1.content_length_not_allowed, 在不允许内容长度的响应上发送了内容长度。
   http1.headers_too_large, 请求头部的总字节大小大于配置的限制。
   http1.invalid_characters, 请求头部包含非法的字符。
   http1.invalid_transfer_encoding, 不合法的 Transfer-Encoding 头部.
   http1.invalid_url, 请求的 URL 不合法。
   http1.too_many_headers, 与此请求一起发送的头部太多。
   http1.transfer_encoding_not_allowed, 传输编码在不允许的响应上发送。
   http1.unexpected_underscore, 在配置不允许的情况下，在头部中发送了 underscore。


Http2 详细信息
~~~~~~~~~~~~~~~~~~~

所有 http2 的详细信息都以 *http2.* 为根。

.. csv-table::
   :header: 名称, 描述
   :widths: 1, 2

    http2.inbound_empty_frames_flood, Envoy 检测到入站 HTTP/2 帧泛洪。
    http2.invalid.header.field, 其中一个 HTTP/2 头部无效。
    http2.outbound_frames_flood, Envoy 检测到一个来自服务器的 HTTP/2 帧泛洪。
    http2.too_many_headers, headers（或 trailers）的数量超过了配置的限制。
    http2.unexpected_underscore, Envoy 被配置为丢掉以 underscores 为开头的头部键的请求。
    http2.unknown.nghttp2.error, nghttp2 遇到未知错误。
    http2.violation.of.messaging.rule, 该流违反了 HTTP/2 的消息传递规则。
