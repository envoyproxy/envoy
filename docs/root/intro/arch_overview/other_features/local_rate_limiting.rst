.. _arch_overview_local_rate_limit:

本地限速
===================

Envoy 通过 :ref:`本地限速过滤器 <config_network_filters_local_rate_limit>` 支持基于 L4 连接的本地（非分布式）限速。

Envoy 还通过 :ref:`HTTP 本地限速过滤器 <config_http_filters_local_rate_limit>` 支持基于 HTTP 请求的本地限速。可以在在监听器级别或者其他特定级别（例如，虚机或路由级别）全局激活该功能。

最后，Envoy 还支持 :ref:`全局限速 <arch_overview_global_rate_limit>`。本地限速可与全局限速结合使用，以减少全局限速服务的负载。
