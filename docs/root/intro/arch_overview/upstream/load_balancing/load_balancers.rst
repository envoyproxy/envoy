.. _arch_overview_load_balancing_types:

支持的负载均衡器
------------------------

当过滤器需要获取到上游集群主机的连接时，集群管理器将使用负载均衡策略来确定选择哪台主机。负载均衡策略是可拔插的，并且在 :ref:`配置 <envoy_v3_api_msg_config.cluster.v3.Cluster>` 中以每个上游集群为基础进行指定。请注意，如果没有为集群 :ref:`配置 <config_cluster_manager_cluster_hc>` 活动的运行状况健康检查策略，则所有上游集群成员都认为是健康的，除非通过 :ref:`health_status <envoy_v3_api_field_config.endpoint.v3.LbEndpoint.health_status>` 进行指定。

.. _arch_overview_load_balancing_types_round_robin:

基于权重的轮询
^^^^^^^^^^^^^^^^^^^^

这是最简单的策略，每个可用的上游主机按照顺序轮询。如果节点的位置有 :ref:`权重
<envoy_v3_api_field_config.endpoint.v3.LbEndpoint.load_balancing_weight>` ，基于权重的轮询调度会被使用，更高权重的节点会更多的被轮询到，从而达到权重的效果。

.. _arch_overview_load_balancing_types_least_request:

基于权重的最少请求
^^^^^^^^^^^^^^^^^^^^^^

最少请求负载均衡器有多种不同的算法，依赖于主机权重是否相同。

* *所有权重全相同*：这是一个 O(1) 算法，通过 :ref:`配置 <envoy_v3_api_msg_config.cluster.v3.Cluster.LeastRequestLbConfig>` （默认为 2）选择 N 个随机可用主机，选择这些主机中活跃请求最少的一台（`Mitzenmacher et al.<https://www.eecs.harvard.edu/~michaelm/postscripts/handbook2001.pdf>` _ 表明这个方法只要 O(N) 次完全扫描)。这个方法也被称为 P2C （两次选择的威力）。P2C 负载均衡器的特点是最高请求量的主机，不会收到新的请求。这使得这台主机允许被放逐，直到它的请求量少于或等于其他主机。

* *所有权重不全相同*：如果两个或者更多的主机有不同的负载均衡权重，负载均衡器会进入另一种模式，使用基于权重的轮询调度，其中权重是根据主机被选中时的请求负载动态调节。

  这种情况下被选中时主机的权重按照如下公式进行计算：`权重 = 负载均衡权重 / (活跃请求量 + 1)^ 活跃请求偏差`。

  :ref:`活跃请求偏差<envoy_v3_api_field_config.cluster.v3.Cluster.LeastRequestLbConfig.active_request_bias>`
  可以在运行时配置，默认为 1。它必须大于等于 0.0。活跃请求偏差越大，更多的请求会降低有效权重。

  如果活跃请求偏差设置为 0.0，最少请求负载均衡器就和轮询负载均衡效果一样了，忽略了选中时刻的活跃请求量。

  例如，如果活跃请求偏差为 1.0，一个主机的权重为 2 活跃请求量为 4 ，那么有效权重为 2 / (4 + 1)^1 = 0.4。算法给稳定状态提供了很好的均衡，但不适合负载极度不平衡。并且，和 P2C 不同，一台主机不会真的放逐，尽管它在一段时间内会收到更少的请求量。

.. _arch_overview_load_balancing_types_ring_hash:

环状哈希
^^^^^^^^^

环状哈希负载均衡器实现了上游主机的一致性哈希。每个主机映射到一个圆环 ("ring") 上通过对它的地址进行哈希；每个请求路由到某个主机，通过哈希请求的某个属性，找到环上顺时针最近的主机。这个技术也被称为 `"Ketama" <https://github.com/RJ/ketama>`_ 哈希，像所有的基于哈希的负载均衡器一样，当协议有一个值可以哈希才有效。

每个主机哈希并按照主机的权重对应环上一定比例的多个位置。例如，主机 A 权重为 1，主机 B 权重为 2，可能会有三个条目出现在环上：一个主机 A 的位置，两个主机 B 的位置。但是这了能并不满足 2:1 的比例划分圆环，因为计算的 hash 可能会让两个主机很接近；所以有必要乘数扩大每个主机的哈希数量，例如插入 100 个主机 A 的条目，200 个主机 B 的条目，这样更接近想要的分布。最佳实践是，指定 :ref:`环最小数量 <envoy_v3_api_field_config.cluster.v3.Cluster.RingHashLbConfig.minimum_ring_size>` 和 :ref:`环最大数量 <envoy_v3_api_field_config.cluster.v3.Cluster.RingHashLbConfig.maximum_ring_size>` ，并监控 :ref:`min_hashes_per_host and max_hashes_per_host
gauges<config_cluster_manager_cluster_stats_ring_hash_lb>` 来确保好的分布。如果环划分合适，增加或删除 N 个主机中的一个，只会影响 1/N 的请求。

当使用基于优先级的负载均衡时，优先级也会被哈希使用，所以只要后端的集合是稳定的，选中的节点会保持一致性。

.. _arch_overview_load_balancing_types_maglev:

Maglev
^^^^^^

Maglev 负载均衡器实现了对上游主机的一致性哈希。它使用 `这篇论文 <https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf>` _ 3.4 小节描述的算法，使用了 65537 固定大小的表格（见文章的 5.3 小节）。Maglev 可以在任何需要一致性哈希的时候完全替代 :ref:`环状哈希负载均衡 <arch_overview_load_balancing_types_ring_hash>` 。和 环状哈希负载均衡器一样，一致性哈希负载均衡器只对协议使用特定哈希值路由时才有效。

构造表格的算法把每个主机按照它的权重以一定比例保存在表中，直到表格被完全填满。例如，主机 A 的权重为 1，主机 B 的权重为 2，那么主机 A 有 21846 个条目，主机 B 有 43691 个条目（总计 65537 个条目）。这个算法尝试把每个主机放入表格中至少一次，无论主机权重和位置权重，这样在一些极端情况下，实际的比例和配置的权重可能不同。例如，主机的总数大于固定表格的大小，那么一些主机获得了一个条目，其余主机无论权重是多少，它的条目数是 0。最佳实践是，监控 :ref:`主机最小条目数和主机最大条目数 gauges <config_cluster_manager_cluster_stats_maglev_lb>` 确保没有主机被表示不足或缺失。

通常，当和环状哈希 ("ketama") 算法比较时，Maglev 有更快的表格查询构建时间和主机选择时间(大概分别时 10 倍和 5 倍当使用一个 256K 的环时)。Maglev 的缺点是，它不是一个稳定的环状哈希。当主机移除时，移动的键值更多（模拟时接近双倍的键值会被移动）。这样来说，许多应用包括 Redis，Maglev 对环状哈希的替换有极大的落差。高级的读者可以用 :repo:` 这个基准测试 </test/common/upstream/load_balancer_benchmark.cc>` 带着不同参数比较 Maglve 和哈希环。

.. _arch_overview_load_balancing_types_random:

随机
^^^^^^

随机负载均器会随机选择一个可用的主机。随机负载均衡器在没有健康检查策略的情况下比轮询性能更好。随机选择避免了偏向发过故障的主机。
