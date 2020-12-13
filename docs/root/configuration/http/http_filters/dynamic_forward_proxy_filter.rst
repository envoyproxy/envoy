.. _config_http_filters_dynamic_forward_proxy:

动态转发代理
=====================

* HTTP 动态转发代理 :ref:`架构总览 <arch_overview_http_dynamic_forward_proxy>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig>`
* 此过滤器的名称应该被配置为 *envoy.filters.http.dynamic_forward_proxy*

下面是一份完整的配置，配置包含
:ref:`动态转发代理 HTTP 过滤器
<envoy_v3_api_msg_extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig>`
和 :ref:`动态转发代理集群
<envoy_v3_api_msg_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig>`。
过滤器和集群必须一起配置，并且指向相同的 DNS 缓存参数，这样 Envoy 才能作为 HTTP 动态转发代理运作。

此过滤器支持 :ref:`主机地址重写 <envoy_v3_api_msg_extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig>`，
通过配置 :ref:`虚拟主机的 typed_per_filter_config 配置 <envoy_v3_api_field_config.route.v3.VirtualHost.typed_per_filter_config>` 或者
:ref:`路由的 typed_per_filter_config 配置 <envoy_v3_api_field_config.route.v3.Route.typed_per_filter_config>` 来实现。 
这可以被用在进行 DNS 查找前重写主机头为指定值，因此允许在转发时将流量路由到重写后的主机地址。
参阅以下示例，其中包含已配置的路由。

.. note::

  使用 *trusted_ca* 证书在集群上配置一项
  :ref:`transport_socket 和名称 envoy.transport_sockets.tls <envoy_v3_api_field_config.cluster.v3.Cluster.transport_socket>`，
  可指示 Envoy 在连接上游主机和验证证书链时使用 TLS。
  此外，Envoy 会自动地为已解析的主机名称进行 SAN 认证，并且通过 SNI 指定主机名称。

.. _dns_cache_circuit_breakers:

  动态转发代理对 DNS 缓存使用内置的熔断器，
  通过配置 :ref:`DNS 缓存熔断器 <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_cache_circuit_breaker>` 来实现。
  默认情况下，此行为被运行时特性 `envoy.reloadable_features.enable_dns_cache_circuit_breakers` 启用。
  如果这个运行时特性被禁用，即使对 :ref:`DNS 缓存熔断器 <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_cache_circuit_breaker>` 进行了设置，但集群熔断器依旧会被使用。

.. literalinclude:: _include/dns-cache-circuit-breaker.yaml
    :language: yaml

统计
----------

动态转发代理 DNS 缓存输出统计在 dns_cache.<dns_cache_name>.* 命名空间中。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  dns_query_attempt, Counter, DNS 查询尝试次数。
  dns_query_success, Counter, DNS 查询成功次数。
  dns_query_failure, Counter, DNS 查询失败次数。
  host_address_changed, Counter, 导致主机地址更改的 DNS 查询次数。
  host_added, Counter, 已经被添加到缓存的主机数。
  host_removed, Counter, 已经从缓存被删除的主机数。
  num_hosts, Gauge, 当前在缓存中的主机数。
  dns_rq_pending_overflow, Counter, 待处理请求溢出的 DNS 数。

动态转发代理 DNS 缓存熔断器输出统计在 *dns_cache.<dns_cache_name>.circuit_breakers*
命名空间。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  rq_pending_open, Gauge, 请求熔断器是关闭 (0) 还是开启 (1)
  rq_pending_remaining, Gauge, 直到熔断器开启，剩余的请求数
