.. _config_udp_listener_filters_udp_proxy:

UDP 代理
=========

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig>`
* 此过滤器的名称应该被配置为 *envoy.filters.udp_listener.udp_proxy* 。

概述
--------

UDP 代理监听器过滤器允许 Envoy 作为 UDP 客户端和服务器之间的 *非透明* 代理进行操作。非透明意味着上游服务器将
看到 Envoy 实例相对于客户端的源 IP 地址和端口。所有的数据报都是从客户端，到 Envoy，再到上游服务器，然后回到 Envoy，
再回到客户端。

由于 UDP 不是面向连接的协议，Envoy 必须保持跟踪客户端的 *session*，以便来自上游服务器的响应数据报可以返回给正确的客户端。
每个会话由 4 元组进行索引，4 元组由源 IP 地址、源端口和接收数据报的本地 IP 地址、本地端口组成。会话会持续到
:ref:`空闲超时 <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.idle_timeout>` 。

如果设置了 :ref:`use_original_src_ip <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig>`
字段，UDP 代理监听器过滤器也可以作为 *透明* 代理运行。但是请记住，它不会把端口转发到上游，它只会把 IP 地址转发到上游。

负载均衡及异常主机的处理
-------------------------

在对 UDP 数据报进行负载均衡时，Envoy 将为配置的上游集群充分利用配置的负载均衡器。创建新会话时，Envoy 将会与
使用配置的负载均衡器选择的上游主机进行关联。将来，所有属于该会话的数据报都将路由到相同的上游主机。

当上游主机发生异常时（由于 :ref:`主动健康检查 <arch_overview_health_checking>` ），
Envoy 将尝试创建一个与健康主机的新会话。

断路
------

每个上游集群可以创建的会话数受集群 :ref:`最大连接断路器 <arch_overview_circuit_break_cluster_maximum_connections>`
的限制。默认情况下为 1024。

示例配置
---------

下面的示例配置，Envoy 将在 UDP 端口 1234 上监听，并代理到监听端口为 1235 的 UDP 服务器。

.. literalinclude:: _include/udp-proxy.yaml
    :language: yaml


统计
------

UDP 代理过滤器发出它自己的下游统计信息以及许多适用的 :ref:`集群上游统计信息 <config_cluster_manager_cluster_stats>`。
下游的统计数据的根是 *udp.<stat_prefix>.*。统计信息如下：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  downstream_sess_no_route, Counter, 由于没有群集而未被路由的数据报数
  downstream_sess_rx_bytes, Counter, 接收的字节数
  downstream_sess_rx_datagrams, Counter, 接收的数据报数
  downstream_sess_rx_errors, Counter, 数据报接收错误数
  downstream_sess_total, Counter, 创建的会话总数
  downstream_sess_tx_bytes, Counter, 传输的字节数
  downstream_sess_tx_datagrams, Counter, 传输的数据报数
  downstream_sess_tx_errors, counter, 数据报传输错误数
  idle_timeout, Counter, 由于空闲超时而销毁的会话数
  downstream_sess_active, Gauge, 当前活动会话数

UDP 代理使用以下标准 :ref:`上游集群统计信息 <config_cluster_manager_cluster_stats>`：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  upstream_cx_none_healthy, Counter, 由于没有正常主机而丢弃的数据报数
  upstream_cx_overflow, Counter, 由于命中会话断路器而丢弃的数据报数
  upstream_cx_rx_bytes_total, Counter, 接收的字节数
  upstream_cx_tx_bytes_total, Counter, 传输的字节数

UDP 代理过滤器还发出以 *cluster.<cluster_name>.udp.* 为前缀的自定义上游群集统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  sess_rx_datagrams, Counter, 接收到的数据报数
  sess_rx_errors, Counter, 数据报接收错误数
  sess_tx_datagrams, Counter, 传输的数据报数
  sess_tx_errors, Counter, 传输错误的数据报数
