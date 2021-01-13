.. _config_http_conn_man_stats:

统计
==========

每一个连接管理器都有一个以 *http.<stat_prefix>.* 为根的统计树，具有以下的统计信息：

.. csv-table::
   :header: 名称, 类型, 描述
   :widths: 1, 1, 2

   downstream_cx_total, Counter, 连接总数
   downstream_cx_ssl_total, Counter, TLS 连接总数
   downstream_cx_http1_total, Counter, HTTP/1.1 连接总数
   downstream_cx_upgrades_total, Counter, 成功升级的连接总数。这些统计数据也包括所有 http1/http2 连接总数。
   downstream_cx_http2_total, Counter, HTTP/2 连接总数
   downstream_cx_destroy, Counter, 销毁的连接总数
   downstream_cx_destroy_remote, Counter, 由于远程关闭而被销毁的连接总数
   downstream_cx_destroy_local, Counter, 由于本地关闭而被销毁的连接总数
   downstream_cx_destroy_active_rq, Counter, 由于超过一个活跃请求而销毁的连接总数
   downstream_cx_destroy_local_active_rq, Counter, 有一个以上活跃请求而被本地销毁的连接总数
   downstream_cx_destroy_remote_active_rq, Counter, 有一个以上活跃请求而被远程销毁的连接总数
   downstream_cx_active, Gauge, 活跃的连接总数
   downstream_cx_ssl_active, Gauge, 活跃的 TLS 连接总数
   downstream_cx_http1_active, Gauge, 活跃的 HTTP/1.1 连接总数
   downstream_cx_upgrades_active, Gauge, 活跃的升级的连接总数。这些统计数据也包括活跃的 http1/http2 连接总数。
   downstream_cx_http2_active, Gauge, 活跃的 HTTP/2 连接总数
   downstream_cx_protocol_error, Counter, 协议错误总数
   downstream_cx_length_ms, Histogram, 连接时长毫秒
   downstream_cx_rx_bytes_total, Counter, 收到的总字节数
   downstream_cx_rx_bytes_buffered, Gauge, 当前收到并缓存的总字节数
   downstream_cx_tx_bytes_total, Counter, 发出的总字节数
   downstream_cx_tx_bytes_buffered, Gauge, 当前已缓存的已发送字节总数
   downstream_cx_drain_close, Counter, 由于排空而关闭的连接总数
   downstream_cx_idle_timeout, Counter, 由于空闲超时而关闭的连接总数
   downstream_cx_max_duration_reached, Counter, 由于最大连接持续时间而关闭的总连接数
   downstream_cx_overload_disable_keepalive, Counter, 由于 Envoy 过载而被禁用 HTTP 1.x keepalive 的连接总数
   downstream_flow_control_paused_reading_total, Counter, 由于流量控制而禁止的总读取次数
   downstream_flow_control_resumed_reading_total, Counter, 由于流量控制而在连接上启用的总读取次数
   downstream_rq_total, Counter, 总请求数
   downstream_rq_http1_total, Counter, HTTP/1.1 总请求数
   downstream_rq_http2_total, Counter, HTTP/2 总请求数
   downstream_rq_active, Gauge, 活跃的总请求数
   downstream_rq_response_before_rq_complete, Counter, 在请求完成之前发送的总响应数
   downstream_rq_rx_reset, Counter, 收到的请求重置总数
   downstream_rq_tx_reset, Counter, 发出的请求重置总数
   downstream_rq_non_relative_path, Counter, 带有非相对 HTTP 路径的请求总数
   downstream_rq_too_large, Counter, 由于缓存超过最大 body 而收到 413 响应的请求总数
   downstream_rq_completed, Counter, 带有响应的请求总数（例如不包括中止的请求）
   downstream_rq_1xx, Counter, 1xx 响应总数
   downstream_rq_2xx, Counter, 2xx 响应总数
   downstream_rq_3xx, Counter, 3xx 响应总数
   downstream_rq_4xx, Counter, 4xx 响应总数
   downstream_rq_5xx, Counter, 5xx 响应总数
   downstream_rq_ws_on_non_ws_route, Counter, 被非升级路由拒绝的升级请求。这个现在适用于 WebSocket 和非 WebSocket 升级。
   downstream_rq_time, Histogram, 请求和响应的总时间（毫秒）
   downstream_rq_idle_timeout, Counter, 由于空闲超时而关闭的请求总数
   downstream_rq_max_duration_reached, Counter, 由于达到了最长持续时间而关闭的请求总数
   downstream_rq_timeout, Counter, 由于请求路径超时而关闭的请求总数
   downstream_rq_overload_close, Counter, 由于 Envoy 过载而关闭的请求总数
   rs_too_large, Counter, 由于缓冲过大的 body 而导致的总响应错误

每 user agent 维度的统计信息
----------------------------

其他每个 user agent 维度进行的统计信息都以 *http.<stat_prefix>.user_agent.<user_agent>.* 开头。 目前 Envoy 匹配 iOS (*ios*) 和 Android (*android*) 的 user agent ，并产生以下的统计信息：

.. csv-table::
   :header: 名称, 类型, 描述
   :widths: 1, 1, 2

   downstream_cx_total, Counter, 连接总数
   downstream_cx_destroy_remote_active_rq, Counter, 由于超过一个活跃请求而被远程销毁的连接总数
   downstream_rq_total, Counter, 请求总数

.. _config_http_conn_man_stats_per_listener:

每监听器的统计信息
-----------------------

其他每个以监听器维度进行的统计信息都以 *listener.<address>.http.<stat_prefix>.* 开头，并有以下统计信息：


.. csv-table::
   :header: 名称, 类型, 描述
   :widths: 1, 1, 2

   downstream_rq_completed, Counter, 所有响应总数
   downstream_rq_1xx, Counter, 1xx 响应总数
   downstream_rq_2xx, Counter, 2xx 响应总数
   downstream_rq_3xx, Counter, 3xx 响应总数
   downstream_rq_4xx, Counter, 4xx 响应总数
   downstream_rq_5xx, Counter, 5xx 响应总数

.. _config_http_conn_man_stats_per_codec:

每编解码器的统计信息
-----------------------

每个编解码器都可以选择添加每个编解码器统计信息。http1 和 http2 都具有编解码器统计信息。

Http1 编解码器统计
~~~~~~~~~~~~~~~~~~~~~~

所有的 http1 统计信息都以 *http1.* 开头

.. csv-table::
   :header: 名称, 类型, 描述
   :widths: 1, 1, 2

   dropped_headers_with_underscores, Counter, 名称中包含下划线的被丢弃的头部总数。这个统计可以通过设置 :ref:`headers_with_underscores_action config setting <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.headers_with_underscores_action>`。
   metadata_not_supported_error, Counter, HTTP/1编码期间被丢弃的元数据总数
   response_flood, Counter, 由于响应泛洪而关闭的连接总数
   requests_rejected_with_underscores_in_headers, Counter, 由于头部名称包含下划线而导致拒绝的请求总数。这个统计可以通过设置 :ref:`headers_with_underscores_action config setting <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.headers_with_underscores_action>`。

Http2 编解码器统计
~~~~~~~~~~~~~~~~~~~~~~

所有的 http2 统计信息都以 *http2.* 开头

.. csv-table::
   :header: 名称, 类型, 描述
   :widths: 1, 1, 2

   dropped_headers_with_underscores, Counter, 名称中包含下划线的被丢弃的头部总数。这个统计可以通过设置 :ref:`headers_with_underscores_action config setting <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.headers_with_underscores_action>`
   header_overflow, Counter,由于头部大于参数 :ref:`configured value <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.max_request_headers_kb>` 而重置的连接总数
   headers_cb_no_stream, Counter, 在没有关联流的情况下进行头部回调的错误总数。由于尚未诊断的 bug，这将跟踪意外发生。
   inbound_empty_frames_flood, Counter, 由于有效载荷为空且没有结束流标志的连续入站帧超出限制而终止的连接总数。这个限制值可以通过设置 :ref:`max_consecutive_inbound_frames_with_empty_payload config setting <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_consecutive_inbound_frames_with_empty_payload>`
   inbound_priority_frames_flood, Counter, 由于超出 PRIORITY 类型的入站帧的限制而终止的连接总数。这个限制值可以通过设置 :ref:`max_inbound_priority_frames_per_stream config setting <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_inbound_priority_frames_per_stream>`.
   inbound_window_update_frames_flood, Counter, 由于超出 WINDOW_UPDATE 类型的入站帧的限制而终止的连接总数。这个限制值可以通过设置 :ref:`max_inbound_window_updateframes_per_data_frame_sent config setting <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_inbound_window_update_frames_per_data_frame_sent>`。
   outbound_flood, Counter, 由于超出所有类型的出站帧的限制而终止的连接总数。这个限制值可以通过设置 :ref:`max_outbound_frames config setting <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_outbound_frames>`
   outbound_control_flood, Counter, "终止的连接总数超过了 PING，SETTINGS 和 RST_STREAM 类型的出站帧的限制。这个限制可以通过设置 :ref:`max_outbound_control_frames config setting <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_outbound_control_frames>`"
   requests_rejected_with_underscores_in_headers, Counter, 由于头部名称包含下划线而被拒绝的请求总数。这个统计可以通过设置 :ref:`headers_with_underscores_action config setting <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.headers_with_underscores_action>`
   rx_messaging_error, Counter, 违反 HTTP/2 规范 `第8节 <https://tools.ietf.org/html/rfc7540#section-8>`_ 的无效接收帧总数。这个结果会体现在 *tx_reset*
   rx_reset, Counter, Envoy 收到的重置流帧总数
   trailers, Counter, 在下游请求中看到的尾部总数
   tx_flush_timeout, Counter, 等待空闲流窗口刷新流剩余部分的流空闲超时总数 :ref:`流空闲超时<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stream_idle_timeout>`
   tx_reset, Counter, Envoy 发送的重置流帧总数
   keepalive_timeout, Counter, 由于 keepalive 超时而关闭的连接总数 :ref:`keepalive timeout <envoy_v3_api_field_config.core.v3.KeepaliveSettings.timeout>`
   streams_active, Gauge, 编解码器观察到的活动流
   pending_send_bytes, Gauge, 当打开/流/连接窗口正在等待写入的当前缓冲的 body 数据（以字节为单位）

.. attention::

  由于编码解码器和 HTTP 连接管理器的流计量不同，HTTP/2 `streams_active` 的计量值可能大于 HTTP 连接管理器 `downstream_rq_active` 的计量值。

追踪统计
------------------

追踪统计信息是在做出追踪决定时发出的。所有追踪统计信息都以 *http.<stat_prefix>.tracing.* 开头，并带有以下统计信息：

.. csv-table::
   :header: 名称, 类型, 描述
   :widths: 1, 1, 2

   random_sampling, Counter, 通过随机抽样可追踪决策的总数
   service_forced, Counter, 通过服务器运行时标识 *tracing.global_enabled* 的可追踪决策的总数
   client_enabled, Counter, 通过请求头部 *x-envoy-force-trace* 设定的可追踪决策的总数
   not_traceable, Counter, 按 request id 分列的不可追踪的决策总数
   health_check, Counter, 通过健康检查的不可追踪的决策总数
