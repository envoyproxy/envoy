.. _arch_overview_http_dynamic_forward_proxy:

HTTP 动态转发代理
==========================

通过结合使用 :ref:`HTTP 过滤器 <config_http_filters_dynamic_forward_proxy>` 和 :ref:`自定义集群 <envoy_v3_api_msg_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig>` ，Envoy 支持 HTTP 动态转发代理。这意味着 Envoy 可以在不事先了解所有已配置 DNS 地址的情况下执行 HTTP 代理的角色，同时仍保留 Envoy 的绝大多数优势，包括异步 DNS 解析。实现原理如下：

* 动态转发代理 HTTP 过滤器用于拦截目标 DNS 主机尚未在缓存中的请求。
* Envoy 会异步解析 DNS 地址，解析完成后会解除拦截等待响应的所有请求。
* 所有 DNS 地址在缓存中的请求都不会被拦截。解析过程与 :ref:`逻辑 DNS <arch_overview_service_discovery_types_logical_dns>` 服务发现类型的工作方式类似，在任何给定时间都会记住单个目标地址。
* 所有已知主机都存储在动态转发代理集群中，这样就可以在 :ref:`admin output <operations_admin_interface>` 中显示它们。
* 特殊的负载平衡器将在转发过程中根据 HTTP host/authority header 选择要使用的正确主机。
* 一段时间未使用的主机将受到清除它们的 TTL 的限制。
* 当上游集群配置 TLS 上下文的时候，Envoy 将自动对已解析的主机名执行 SAN 验证，并通过 SNI 指定主机名。

上面的实现细节表示，在稳定状态下，Envoy 可以转发大量 HTTP 代理流量，而所有 DNS 解析都在后台异步发生。此外，所有其他 Envoy 过滤器和扩展都可以与动态转发代理支持（包括身份验证，RBAC，速率限制等）一起使用。

有关更多配置信息，请参阅 :ref:`HTTP 过滤器配置文档 <config_http_filters_dynamic_forward_proxy>` 。

内存使用情况详细信息
--------------------

Envoy 的动态转发代理支持的内存使用情况详细信息如下：

* 每个解析的主机/端口对均使用固定的服务器全局内存量，并在所有工作线程之间共享。
* 地址更改是使用读/写锁以内联方式执行的，不需要主机重新分配。
* 通过 TTL 删除的主机会在所有活动的连接停止引用它们并且所有使用的内存都被回收了的情况下被清除。
* :ref:`max_hosts <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.max_hosts>` 字段可以被用来限制 DNS 缓存存储主机的数量。
* 集群的 :ref:` max_pending_requests <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_pending_requests>` 熔断器可用于限制等待 DNS 缓存加载主机的请求数量。
* 长期存在的上游连接可以使下面的逻辑主机通过 TTL 失效，而连接仍处于打开状态。上游请求和连接仍受其他集群熔断器的约束，例如： :ref:` max_requests <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_requests>` 。当前的假设是，与连接和请求本身相比，在连接之间共享的主机数据使用的内存量很小，因此不值得独立控制。