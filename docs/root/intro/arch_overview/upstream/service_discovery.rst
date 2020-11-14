.. _arch_overview_service_discovery:

服务发现
=================

当在 Envoy :ref:`配置 <envoy_v3_api_msg_config.cluster.v3.Cluster>` 中定义了一个上游集群时，Envoy 需要知道如何解析集群的成员。这被称为 *服务发现*。

.. _arch_overview_service_discovery_types:

支持的服务发现类型
---------------------------------

.. _arch_overview_service_discovery_types_static:

静态
^^^^^^

静态是最简单的服务发现类型。配置中明确指导了每个上游主机的解析网络名（IP 地址/端口、Unix 域套接字等）。

.. _arch_overview_service_discovery_types_strict_dns:

严格 DNS
^^^^^^^^^^

当使用严格 DNS 服务发现时，Envoy 将连续和异步解析指定的 DNS 目标。DNS 结果中的每个返回的 IP 地址将被视为上游集群中的一个显式主机。这意味着，如果查询返回三个 IP 地址，Envoy 将认为集群有三个主机，并且这三个主机都应该被负载均衡到。如果有主机从结果中被移除，Envoy 会认为它不再存在，并且会将它的流量从所有的当前连接池中驱逐。因此，如果一个成功的 DNS 解析返回 0 个主机，Envoy 将认为该集群没有任何主机。请注意，Envoy 从不同步解析转发路径中的 DNS。以牺牲最终的一致性为代价，永远不会担心在长期运行的 DNS 查询上出现阻塞。

如果一个 DNS 名称多次解析到同一个 IP，这些 IP 将被去重。

如果多个 DNS 名解析到同一个 IP，健康检查将 *不共享*。这意味着，在使用解析到相同 IP 的 DNS 名称做主动健康检查时需要注意：如果一个 IP 在 DNS 名称之间多次重复，可能会对上游主机造成不必要的负载。

如果启用了 :ref:`respect_dns_ttl <envoy_v3_api_field_config.cluster.v3.Cluster.respect_dns_ttl>`，则使用 DNS 记录 TTL 和 :ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` 来控制 DNS 刷新率。对于严格的 DNS 集群，如果所有记录 TTL 的最小值为 0，则 :ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` 用于控制 DNS 刷新率。 :ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` 如果没有指定，默认为 5000ms。:ref:`dns_failure_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_failure_refresh_rate>` 控制故障时的刷新频率，如果没有配置，将使用 DNS 刷新率。

DNS 解析会输出 :ref:`集群统计 <config_cluster_manager_cluster_stats>` 字段 *update_attempt*、*update_success* 和 *update_failure*。

.. _arch_overview_service_discovery_types_logical_dns:

逻辑 DNS
^^^^^^^^^^^

逻辑 DNS 使用与严格 DNS 类似的异步解析机制。但是，这种策略并非严格基于 DNS 的查询结果，并认为它们包含整个上游集群，而是只在 *需要建立新连接时* 才使用返回的第一个 IP 地址。因此，一个逻辑连接池可能包含到各种不同上游主机的物理连接。连接永远不会被耗尽，包括在 DNS 解析成功，返回 0 个主机的情况下。

这种服务发现类型是必须通过 DNS 访问的大规模网络服务的最佳选择。这类服务通常使用轮询 DNS 来返回许多不同的 IP 地址。通常情况下，每次查询都会返回不同的结果。如果在这种情况下使用严格 DNS，Envoy 会认为集群的成员在每个解析间隔期间都在变化，这将导致连接池驱逐、连接循环等。取而代之的是，使用逻辑 DNS，连接会保持存活，直到它们被循环。在与大型 Web 服务交互时，这几乎是所有可行策略中最好的方式：异步/最终一致的 DNS 解析，长期维持的连接，以及转发路径中的零阻塞。

如果启用了 :ref:`respect_dns_ttl <envoy_v3_api_field_config.cluster.v3.Cluster.respect_dns_ttl>`，则用 DNS 记录 TTL 和 :ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` 来控制 DNS 刷新率。对于逻辑 DNS 集群，如果第一条记录的 TTL 为 0，则 :ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` 用于控制 DNS 刷新率。 :ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` 如果没有指定，默认为 5000ms。:ref:`dns_failure_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_failure_refresh_rate>` 控制故障时的刷新频率，如果没有配置，将使用 DNS 刷新率。

DNS 解析会输出 :ref:`集群统计 <config_cluster_manager_cluster_stats>` 字段 *update_attempt*、*update_success* 和 *update_failure*。

.. _arch_overview_service_discovery_types_original_destination:

原始目的地
^^^^^^^^^^^^^^^^^^^^

当传入的连接通过 iptable REDIRECT 或 TPROXY 目标或使用代理协议重定向到 Envoy 时，可以使用原始目标集群。在这些情况下，路由到原始目标集群的请求会被转发到重定向元数据所寻址的上游主机，而不需要任何明确的主机配置或上游主机发现。连接到上游主机的连接会被池化，当闲置时间超过 :ref:`cleanup_interval <envoy_v3_api_field_config.cluster.v3.Cluster.cleanup_interval>` 时，未使用的主机会被清理，默认值为 5000ms。如果原始目的地不可用，则不创建上行连接。Envoy 也可以从 :ref:`HTTP 头 <arch_overview_load_balancing_types_original_destination_request_header>` 中获取原始目的地。原始目的地服务发现必须与原始目的地 :ref:`负载均衡 <arch_overview_load_balancing_types_original_destination>` 一起使用。

.. _arch_overview_service_discovery_types_eds:

端点发现服务 (EDS)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

*端点发现服务* 是一个 :ref:`基于 gRPC 或 REST-JSON API 的 xDS 管理服务器<config_overview_management_server>`，Envoy 通过它来获取集群成员。在 Envoy 的术语中，集群成员被称为“端点”。对于每个集群，Envoy 从发现服务中获取端点。EDS 是首选的服务发现机制，原因有以下几点：

* Envoy 对每个上游主机都有明确的了解（相对于通过 DNS 解析的负载均衡器进行路由），可以做出更智能的负载均衡决策。
* 每台主机的服务发现 API 响应中携带的额外属性会告知 Envoy 该主机的负载均衡权重、金丝雀状态、区域等。这些额外的属性在负载均衡、统计收集等过程中被 Envoy 网格全局使用。

Envoy 项目提供了 EDS 和 :ref:`其他发现服务 <arch_overview_dynamic_config>` 的 `Java <https://github.com/envoyproxy/java-control-plane>`_ 和 `Go <https://github.com/envoyproxy/go-control-plane>`_ 语言版本的参考 gRPC 实现。

.. _arch_overview_service_discovery_types_custom:

自定义集群
^^^^^^^^^^^^^^

Envoy 还支持自定义集群发现机制。自定义集群使用 :ref:`cluster_type 字段 <envoy_v3_api_field_config.cluster.v3.Cluster.cluster_type>` 在集群配置上指定。

一般情况下，主动健康检查与最终一致的服务发现服务数据一起使用，以做出负载均衡和路由决策。这将在下一节进一步讨论。

.. _arch_overview_service_discovery_eventually_consistent:

关于最终一致的服务发现
------------------------------------------

许多现有的 RPC 系统把服务发现当作一个完全一致的过程。为此，他们使用完全一致性的领导者选举的备份存储，如 Zookeeper、etcd、Consul 等。我们的经验是，大规模运营这些备份存储是很痛苦的。

Envoy 从设计之初就考虑到服务发现不需要完全一致。相反，Envoy 假设主机以最终一致的方式从网格中来来去去。我们推荐的部署服务到服务的 Envoy 网格的方式使用最终一致的服务发现以及 :ref:`主动健康检查 <arch_overview_health_checking>` （Envoy 明确地对上游集群成员进行健康检查）来确定集群健康状况。这种模式有很多好处：

* 所有的健康决策都是完全分布式的。因此，网络分区会被优雅地处理（应用程序是否优雅地处理分区是另一回事）。
* 当为上游集群配置健康检查时，Envoy 使用 2x2 矩阵来决定是否路由到主机：

.. csv-table::
  :header: 服务发现状态, 健康检查正常, 健康检查失败
  :widths: 1, 1, 2

  已发现, 执行路由, 不执行路由
  未发现, 执行路由, 不执行路由/移除

发现主机/健康检查正常
  Envoy **将路由** 到目标主机。

未发现主机/健康检查正常
  Envoy **将路由** 到目标主机。这一点非常重要，因为设计中假设发现服务随时可能失败。如果一个主机在发现数据中不存在后仍然通过健康检查，Envoy 仍然会进行路由。虽然在这种情况下不可能增加新的主机，但现有的主机将继续正常运行。当发现服务再次正常运行时，数据最终会重新合并。

发现主机/健康检查失败
  Envoy **不会路由** 到目标主机。健康检查数据被认为比发现数据更准确。

未发现主机/健康检查失败
  Envoy **不会路由，并会移除** 目标主机。这是唯一一种 Envoy 会清除主机数据的状态。
