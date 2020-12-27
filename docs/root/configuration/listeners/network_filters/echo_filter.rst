.. _config_network_filters_echo:

Echo
====

echo 是一个简单的网络过滤器，主要用于演示网络过滤器 API。如果安装了，那么它会将接收到的数据回送（写入）到连接的下游客户端。
过滤器的名称应该配置为 *envoy.filters.network.echo* 。

* :ref:`v3 API 参考 <envoy_v3_api_field_config.listener.v3.Filter.name>`
