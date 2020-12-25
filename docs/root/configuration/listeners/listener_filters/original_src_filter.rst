.. _config_listener_filters_original_src:

原始源
===============

* :ref:`监听器过滤器 v3 API 参考 <envoy_v3_api_msg_extensions.filters.listener.original_src.v3.OriginalSrc>`
* 此过滤器的名称应该被配置为 *envoy.filters.listener.original_src*。

在 Envoy 的上游侧，原始源监听器过滤器复制连接的下游远程地址。例如，如果下游连接以IP地址 ``10.1.2.3`` 连接到 Envoy，则 Envoy 将以源 IP ``10.1.2.3`` 连接到上游。

与代理协议的交互
--------------------------------

如果连接尚未转换或代理其源地址，则 Envoy 可以简单地使用现有的连接信息来构建正确的下游远程地址。 但是，如果不正确，则可以使用 :ref:`代理协议过滤器 <config_listener_filters_proxy_protocol>` 提取下游远程地址。

IP 版本支持
------------------
该过滤器同时支持 IPv4 和 IPv6 作为地址。 注意上游连接必须支持所使用的版本。

额外设置
-----------

使用的下游远程地址很可能是全局可路由的。 默认情况下，从上游主机返回到该地址的数据包将不会通过 Envoy 路由。必须将网络配置为通过 Envoy 主机强制路由回所有 IP 被 Envoy 复制的流量。

如果 Envoy 和上游在同一主机上 -- 例如在 Sidecar 部署中，则可以使用 iptables 和路由规则来确保正确的行为。过滤器具有无符号整数配置，即 :ref:`mark <envoy_v3_api_field_extensions.filters.listener.original_src.v3.OriginalSrc.mark>` 。将此设置为 *X* 会导致 Envoy 用 *X* 值 *标记* 来自此监听器的所有上游数据包。 注意如果将 :ref:`mark <envoy_v3_api_field_extensions.filters.listener.original_src.v3.OriginalSrc.mark>` 的值设置为 0，Envoy 将不会标记上游数据包。

We can use the following set of commands to ensure that all ipv4 and ipv6 traffic marked with *X*
(assumed to be 123 in the example) routes correctly. Note that this example assumes that *eth0* is
the default outbound interface.
我们可以使用以下命令集来确保所有标有 *X*（在示例中假定为 123）的 ipv4 和 ipv6 流量正确路由。 注意此示例假定 *eth0* 为
默认出站接口。

.. code-block:: text

  iptables  -t mangle -I PREROUTING -m mark     --mark 123 -j CONNMARK --save-mark
  iptables  -t mangle -I OUTPUT     -m connmark --mark 123 -j CONNMARK --restore-mark
  ip6tables -t mangle -I PREROUTING -m mark     --mark 123 -j CONNMARK --save-mark
  ip6tables -t mangle -I OUTPUT     -m connmark --mark 123 -j CONNMARK --restore-mark
  ip rule add fwmark 123 lookup 100
  ip route add local 0.0.0.0/0 dev lo table 100
  ip -6 rule add fwmark 123 lookup 100
  ip -6 route add local ::/0 dev lo table 100
  echo 1 > /proc/sys/net/ipv4/conf/eth0/route_localnet


监听器配置示例
------------------------------

下面的示例将 Envoy 配置为对端口 8888 上的所有连接使用原始源。它使用代理协议来确定下游远程地址。所有上游数据包被标记为 123。

.. code-block:: yaml

  listeners:
  - address:
      socket_address:
        address: 0.0.0.0
        port_value: 8888
    listener_filters:
      - name: envoy.filters.listener.proxy_protocol
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.listener.proxy_protocol.v3.ProxyProtocol
      - name: envoy.filters.listener.original_src
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.listener.original_src.v3.OriginalSrc
          mark: 123
