.. _arch_overview_tcp_proxy:

TCP 代理
=========

由于 Envoy 本质上是一个 L3/L4 服务器，因此它很容易实现基本的 L3/L4 网络代理功能。
TCP 代理过滤器可以在下游客户端和上游集群之间进行最基本的 1:1 网络连接代理。
它可以单独用作安全隧道的替代品，也可以与其他过滤器结合使用（例如 :ref:`MongoDB 过滤器 <arch_overview_mongo>` 或 :ref:`速率限制过滤器 <config_network_filters_rate_limit>`）。

TCP 代理过滤器将遵循每个上游集群的全局资源管理器配置的:ref:`连接限制 <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_connections>`。
TCP 代理过滤器会与上游集群的资源管理器共同协商能否在不超过该集群的最大连接数的限制条件下创建新连接。如果不满足该限制条件，TCP 代理将不会创建连接。

TCP 代理过滤器 :ref:`参考配置 <config_network_filters_tcp_proxy>`。
