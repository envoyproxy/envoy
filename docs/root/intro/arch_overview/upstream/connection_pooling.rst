.. _arch_overview_conn_pool:

连接池
==================

对于 HTTP 流量，Envoy 支持在底层线协议（HTTP/1.1 或 HTTP/2）之上分层的抽象连接池。利用过滤器代码不需要知道底层协议是否支持真正的复用。在实践中，底层实现具有以下高级属性：

HTTP/1.1
--------

HTTP/1.1 连接池根据需要获取上游主机的连接（最高到断路限制）。当连接可用时，请求会被绑定到连接上，这可能是因为一个连接已经完成了对前一个请求的处理，或者是因为一个新的连接已经准备好接收它的第一个请求。HTTP/1.1 连接池没有使用管道，因此如果上游连接断开，只需重置一个下游请求。

HTTP/2
------

HTTP/2 连接池在一个连接上多路传输多个请求，但不超过 :ref:`最大并发流 <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_concurrent_streams>` 和 :ref:`每个连接的最大请求量 <envoy_v3_api_field_config.cluster.v3.Cluster.max_requests_per_connection>` 的限制。HTTP/2 连接池建立了服务请求所需的连接数。在没有限制的情况下，这将只是一个单一的连接。如果收到 GOAWAY 帧，或者连接达到了 :ref:`每个连接的最大请求量 <envoy_v3_api_field_config.cluster.v3.Cluster.max_requests_per_connection>` 的限制，连接池将驱逐受影响的连接。一旦一个连接达到其 :ref:`最大并发流限制 <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_concurrent_streams>`，它将被标记为繁忙，直到有流可用。只要有待处理的请求而没有可以调度到的连接，就会建立新的连接（最多为连接的断路限制）。HTTP/2 是首选的通信协议，因为连接很少（如果有的话）会被切断。

.. _arch_overview_conn_pool_how_many:

连接池数量
--------------------------

每个集群中的每台主机都会有一个或多个连接池。如果集群只有 HTTP/1 或 HTTP/2，那么主机可能只有一个连接池。但是，如果集群支持多个上游协议，那么每个协议将至少分配一个连接池。还会为以下每个功能分配单独的连接池：

* :ref:`路由优先级 <arch_overview_http_routing_priority>`
* :ref:`套接字选项 <envoy_v3_api_field_config.core.v3.BindConfig.socket_options>`
* :ref:`传输套接字（如 TLS）选项 <envoy_v3_api_msg_config.core.v3.TransportSocket>`

每个工作线程为每个集群维护自己的连接池，因此，如果一个 Envoy 有两个线程和一个同时支持 HTTP/1 和 HTTP/2 的集群，那么至少会有 4 个连接池。

.. _arch_overview_conn_pool_health_checking:

健康检查互动
----------------------------

如果 Envoy 被配置为主动或被动 :ref:`健康检查 <arch_overview_health_checking>`，所有的连接池连接将被关闭表示主机从可用状态过渡到不可用状态。如果主机重新进入负载均衡轮转，它将创建新的连接，这可以最大程度的解决不良流量（由于 ECMP 路由或其他原因）。
