.. _config_network_filters_tcp_proxy:

TCP 代理
=========

* TCP 代理 :ref:`架构概览 <arch_overview_tcp_proxy>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy>`
* 过滤器的名称应该配置为 *envoy.filters.network.tcp_proxy* 。

.. _config_network_filters_tcp_proxy_dynamic_cluster:

动态集群选择
-------------------------

TCP 代理过滤器使用的上游集群可以由其他网络过滤器根据每个连接动态设置，方法是在 `envoy.tcp_proxy.cluster` 字段下设置每个连接的状态对象。见实现详情。

.. _config_network_filters_tcp_proxy_subset_lb:

路由到主机子集
----------------------------

可以配置 TCP 代理，让它路由到上游集群内部的主机子集。

为了定义合适的上游主机必须匹配的元数据，请使用以下字段之一：

#. 使用 :ref:`TcpProxy.metadata_match<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.metadata_match>` 为单个上游集群定义所需的元数据。
#. 使用 :ref:`ClusterWeight.metadata_match<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.WeightedCluster.ClusterWeight.metadata_match>` 为权重集群定义所需的元数据。
#. 使用 :ref:`TcpProxy.metadata_match<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.metadata_match>` 和 :ref:`ClusterWeight.metadata_match<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.WeightedCluster.ClusterWeight.metadata_match>` 的组合为权重上游集群定义所需的元数据（后者的元数据将在前者的基础上合并）。

此外，元数据可以由 `StreamInfo` 上的早期网络过滤器设置。设置元数据必须在 `TcpProxy` 过滤器调用 `onNewConnection()` 前发生，以此来影响负载均衡。

.. _config_network_filters_tcp_proxy_stats:

统计信息
----------

TCP 代理过滤器即会发出自己的下游统计信息，也会在适用的情况下发出许多 :ref:`上游集群统计信息 <config_cluster_manager_cluster_stats>` 。下游统计信息以 *tcp.<stat_prefix>.* 为根，如下所示的统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  downstream_cx_total, Counter, 过滤器处理的连接总数
  downstream_cx_no_route, Counter, 未找到路由或者未找到路由集群的连接总数
  downstream_cx_tx_bytes_total, Counter, 写入下游连接的字节总数
  downstream_cx_tx_bytes_buffered, Gauge, 当前缓冲到下游连接的字节总数
  downstream_cx_rx_bytes_total, Counter, 从下游连接读取的字节总数
  downstream_cx_rx_bytes_buffered, Gauge, 当前从下游连接缓冲的字节总数
  downstream_flow_control_paused_reading_total, Counter, 流控暂停从下游读取数据的总次数
  downstream_flow_control_resumed_reading_total, Counter, 流控恢复从下游读取数据的总次数
  idle_timeout, Counter, 由于空闲超时而关闭的连接总数
  max_downstream_connection_duration, Counter, 由于 max_downstream_connection_duration 超时而关闭的连接总数
  upstream_flush_total, Counter, 关闭下游连接后，持续刷新上游数据的连接总数
  upstream_flush_active, Gauge, 关闭下游连接后，当前持续刷新上游连接的连接总数
