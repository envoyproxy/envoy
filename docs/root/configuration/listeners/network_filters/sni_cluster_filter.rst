.. _config_network_filters_sni_cluster:


SNI的上游集群
=========================

`sni_cluster` 是一个在TLS连接中使用SNI值作为上游集群名称的网络过滤器。在非TLS连接中，此过滤器不会修改上游集群。此过滤器应该以名称 *envoy.filters.network.sni_cluster* 来配置。

这个过滤器没有配置项。必须在 :ref:`tcp_proxy <config_network_filters_tcp_proxy>` 过滤器之前安装。

* :ref:`v3 API reference <envoy_v3_api_field_config.listener.v3.Filter.name>`。
