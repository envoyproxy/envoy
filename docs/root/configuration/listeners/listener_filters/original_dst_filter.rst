.. _config_listener_filters_original_dst:

原目的地
====================

当连接已经由 iptables REDIRECT 目标，或由设置了监听器 :ref:`透明 <envoy_v3_api_field_config.listener.v3.Listener.transparent>` 选项的 iptables TPROXY 目标进行了重定向。原目的地监听器过滤器会读取 SO_ORIGINAL_DST 套接字选项设置。Envoy 中的后续处理会将恢复的目的地地址视为连接的本地地址，而不是监听器正在监听的地址。此外， :ref:`原始目标群集  <arch_overview_service_discovery_types_original_destination>` 可能被用来将 HTTP 请求或者 TCP 连接转发到恢复的目的地地址。

* :ref:`v2 API 参考 <envoy_v3_api_field_config.listener.v3.ListenerFilter.name>`
* 此过滤器的名称应该被配置为 *envoy.filters.listener.original_dst*。
