DNS 过滤器
==========

Envoy 支持通过配置 :ref:`UDP 监听器 DNS 过滤器 <config_udp_listener_filters_dns_filter>` 来响应DNS请求。

DNS 过滤器支持响应对 A 和 AAAA 记录的转发查询。从静态配置的资源、群集或外部 DNS 服务器中找到查询结果。
过滤器将返回不超过512个字节的 DNS 响应。如果为域名配置了多个 IP 地址，或者为群集配置了多个端点，Envoy 
将返回查找到的每个地址，并且返回数据的大小不超过上述的大小限制。
