DNS 过滤器
==========

Envoy 支持通过配置 :ref:`UDP 监听 DNS 过滤器 <config_udp_listener_filters_dns_filter>` 来响应 DNS 请求。

DNS 过滤器支持响应对 A 和 AAAA 记录的转发查询。从静态配置的资源、集群或外部 DNS 服务器中找到查询结果。
过滤器将返回不超过 512 个字节的 DNS 响应。如果为域名配置了多个地址，或者集群的多个端点，Envoy 
将按照不超过上面提到的最大限制返回查找到的每个地址。
