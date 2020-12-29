.. _config_network_filters_sni_dynamic_forward_proxy:

SNI 动态转发代理
=========================

.. attention::

  SNI 动态转发代理目前还在 alpha 阶段，还不能用于生产环境。

通过 :ref:`TLS 检查 <config_listener_filters_tls_inspector>` 网络监听过滤器，这个网络过滤器和 :ref:`动态转发代理集群 <envoy_api_msg_config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig>` 的组合，Envoy 支持基于 SNI 的动态转发代理。
该实现的机制与 :ref:`HTTP 动态转发代理 <arch_overview_http_dynamic_forward_proxy>` 类似，但使用 SNI 中的值作为目标主机。

以下是完整的配置，该配置包含了这个过滤器和 :ref:`动态转发代理集群 <envoy_api_msg_config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig>` 。过滤器和集群都必须一起配置并且指向相同的 DNS 缓存参数，以便让 Envoy 作为 SNI 动态转发代理。

.. note::

  下面的配置没有终止监听器的 TLS，因此不需要在集群中配置 TLS 上下文，TLS 握手机制由 Envoy 传递。

.. literalinclude:: _include/sni-dynamic-forward-proxy-filter.yaml
    :language: yaml
