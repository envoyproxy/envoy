.. _arch_overview_listeners:

监听器
======

Envoy 支持单个进程中配置任意数量的监听器。通常情况下，部署 Envoy 的数量与监听器数量无关，建议每台机器部署一个 Envoy 即可。这使得操作会更加简便，而且每台机器只有一个监控数据来源。Envoy 同时支持 TCP 和 UDP 监听器。

TCP
---

每个监听器都独立配置了多个 :ref:`过滤器链 <envoy_v3_api_msg_config.listener.v3.FilterChain>`，其中根据其 :ref:`匹配条件 <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>` 选择某个过滤器链。
一个独立的过滤器链由一个或多个网络层(L3/L4) :ref:`过滤器 <arch_overview_network_filters>` 组成。
当监听器上接收到新连接时，会选择适当的过滤器链，接着实例化配置的本地筛选器堆栈和处理后续事件。
通用监听器系统架构通常用于处理各不相同的代理任务，例如 Envoy 用于（ :ref:`限速 <arch_overview_global_rate_limit>`、:ref:`TLS 客户端身份验证 <arch_overview_ssl_auth_filter>`、:ref:`HTTP 连接管理
<arch_overview_http_conn_man>`、MongoDB :ref:`嗅探 <arch_overview_mongo>`、原始 :ref:`TCP 代理 <arch_overview_tcp_proxy>` 等）。

监听器还可以选择配置一些 :ref:`监听过滤器 <arch_overview_listener_filters>`。
这些过滤器在网络层过滤器之前处理，并且有机会去操作连接元数据，这样通常是为了影响后续过滤器或集群如何处理连接。

还可以通过 :ref:`监听器发现服务 (LDS) <config_listeners_lds>` 动态获取监听器。

监听器 :ref:`配置 <config_listeners>` 。

.. _arch_overview_listeners_udp:

UDP
---

Envoy 还支持 UDP 监听器和 :ref:`UDP 监听过滤器 <config_udp_listener_filters>` 。
每个工作线程都会实例化一次 UDP 监听过滤器，并且 UDP 监听过滤器对于工作线程来讲是全局可见的。
每个工作线程监听端口接收到的 UDP 数据报文由监听过滤器处理。
实际上，UDP 监听器配置有一个 ``SO_REUSEPORT`` 内核选项，这将导致内核会将属于同一个 UDP 四个元组的 UDP 报文分配给同一个工作线程。
这就允许 UDP 监听过滤器在需要时可以打开会话保持功能。
这个功能的一个内置示例就是 :ref:`UDP 代理 <config_udp_listener_filters_udp_proxy>` 监听过滤器。
