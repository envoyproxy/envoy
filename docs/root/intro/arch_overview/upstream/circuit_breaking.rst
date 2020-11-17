.. _arch_overview_circuit_break:

断路
================

断路是分布式系统的重要组成部分。在分布式系统中最好是迅速失败，并尽快向下游施加反压。 Envoy 网络的主要好处之一是，Envoy 在网络级别强制执行断路限制，而不是必须独立给每个应用程序配置和编码。Envoy 支持各种类型的全分布式（非协调）断路：

.. _arch_overview_circuit_break_cluster_maximum_connections:

* **群集最大连接数**：Envoy 和上游集群中所有主机建立的最大连接数。如果该断路器溢出，集群的 :ref:`upstream_cx_overflow <config_cluster_manager_cluster_stats>` 计数器将增加。所有的连接，不管是活动的还是空闲的，都会计入这个计数器并由它来限制。即使这个断路器已经溢出，Envoy 也会确保群集负载均衡选择的主机至少有一个连接分配。这就意味着：集群的 :ref:`upstream_cx_active <config_cluster_manager_cluster_stats>` 计数可能会高于集群最大连接断路器，其上限为`集群最大连接数 +（集群的端点数）*（集群的连接池）`。这个边界适用于所有工作者线程的连接数之和。参见 :ref:`连接池 <arch_overview_conn_pool_how_many>`，了解一个集群可能有多少个连接池。
* **集群最大待处理请求**：在等待就绪连接池连接时排队的最大请求数。每当没有足够的上游连接可用来立即调度请求时，请求就会被添加到待处理请求列表中。对于 HTTP/2 连接，如果 :ref:`最大并发流<envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_concurrent_streams>` 和 :ref:`每个连接的最大请求数 <envoy_v3_api_field_config.cluster.v3.Cluster.max_requests_per_connection>` 没有配置，所有的请求都会在同一个连接上被复用，所以只有在还没有建立连接的时候，才会触发这个断路器。如果这个断路器溢出，集群的 :ref:`upstream_rq_pending_overflow <config_cluster_manager_cluster_stats>` 计数器将递增。
* **群集最大请求量**：在任何特定时间对集群中所有主机的最大请求数。如果该断路器溢出，集群的 :ref:`upstream_rq_pending_overflow <config_cluster_manager_cluster_stats>` 计数器将增加。
* **群集最大有效重试**：集群中ß所有主机在任何特定时间内都可以进行的最大重试次数。一般来说，我们建议使用 :ref:`重试预算 <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.retry_budget>`；但是，如果静态断路是首选项，则应该积极断路重试。这样可以允许零星故障的重试，但总体重试量不能爆炸，不能造成大规模的级联故障。如果这个断路器溢出，集群的 :ref:`upstream_rq_retry_overflow <config_cluster_manager_cluster_stats>` 计数器会递增。

  .. _arch_overview_circuit_break_cluster_maximum_connection_pools:

* **集群最大并发连接池**：可并发实例化的最大连接池数量。有些功能，如 :ref:`源监听器过滤器 <arch_overview_ip_transparency_original_src_listener>`，可以创建无限制数量的连接池。当一个集群用尽了它的并发连接池，它将尝试回收一个空闲的连接池。如果不能，那么断路器将溢出。这与 :ref:`集群最大连接数 <arch_overview_circuit_break_cluster_maximum_connections>` 不同的是，连接池永远不会超时，而连接通常会超时。连接会自动清理，而连接池不会。需要注意的是，为了让连接池发挥作用，它至少需要一个上游连接，所以这个值很可能不应该大于 :ref:`集群最大连接数 <arch_overview_circuit_break_cluster_maximum_connections>`。如果这个断路器溢出，集群的 :ref:`upstream_cx_pool_overflow <config_cluster_manager_cluster_stats>` 计数器将递增。

每个断路器的限制是 :ref:`可配置的 <config_cluster_manager_cluster_circuit_breakers>`，并按每个上游集群和每个优先级进行跟踪。这使得分布式系统的不同组件可以独立调整，并有不同的限制。可以通过 :ref:`统计信息 <config_cluster_manager_cluster_stats_circuit_breakers>` 来观察这些断路器的实时状态，包括在断路器打开之前剩余的资源数量。

工作线程共享断路器限制，即如果活动连接阈值为 500，工作线程 1 有 498 个活动连接，那么工作线程 2 只能再分配 2 个连接。由于实现最终是一致的，线程之间的竞赛可能会让限制可能超出。

断路器是默认启用的，并且有适度的默认值，例如每个集群有 1024 个连接。要禁用断路器，请将其 :ref:`阈值 <faq_disable_circuit_breaking>` 设置为允许的最高值。

需要注意的是，在 HTTP 请求中，断路会导致 :ref:`x-envoy-overloaded <config_http_filters_router_x-envoy-overloaded_set>` 头被路由器过滤器设置。
