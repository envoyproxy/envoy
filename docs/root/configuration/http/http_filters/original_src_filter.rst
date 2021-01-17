.. _config_http_filters_original_src:

请求源
==========

* :ref:`HTTP 过滤器 v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.original_src.v3.OriginalSrc>`
* 此过滤器的名称应该配置为 *envoy.filters.http.original_src*。

请求源 http 过滤器在 Envoy 的上游端复制连接的下游远程地址。比如，下游使用 IP 地址 ``10.1.2.3`` 连接到 Envoy，接着 Envoy 使用源 IP ``10.1.2.3`` 连接到上游。下游远程地址是根据 :ref:`XFF <config_http_conn_man_headers_x-forwarded-for>` 中概述的“可信客户端地址”的逻辑确定。


请注意，该过滤器旨在与 :ref:`路由器 <config_http_filters_router>` 过滤器结合使用。特别是，它必须在路由器过滤器之前运行，以便可以将所需的源IP添加到过滤器链的状态。

IP 版本支持
---------------
该过滤器同时支持将 IPv4 和 IPv6 作为地址。请注意，上游连接必须支持所使用的版本。

额外设置
------------

使用的下游远程地址很可能是全局可路由的。默认情况下，从上游主机返回到该地址的数据包将不会通过 Envoy 路由。必须将网络配置为将 IP 被 Envoy 复制的所有流量都通过 Envoy 主机强制路由回去。

假如 Envoy 和上游在同一个主机上--例如在边车（sidecar）部署中--，iptables 和路由规则可以被用来确保正确的行为。过滤器有个无符号整数配置 :ref:`标记 <envoy_v3_api_field_extensions.filters.http.original_src.v3.OriginalSrc.mark>`。将其设置为 *X* 会导致 Envoy对来自此 http 的所有上游数据包 *标记* 为 *X*。注意 :ref:`标记 <envoy_v3_api_field_extensions.filters.http.original_src.v3.OriginalSrc.mark>` 被设置为 0，Envoy 将不会标记上游数据包。

我们可以使用下面这组命令来确保所有被标记为 *X*（例子上假设为 123）的 ipv4 和 ipv6 的流量都进行了正确路由。注意，这个例子假设 *eth0* 为默认的出站接口。

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


HTTP 配置示例
-------------------

下面的示例将 Envoy 配置为对 8888 端口上的所有连接使用请求源。
所有上游数据包被标记为 123。

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.original_src
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.listener.original_src.v3.OriginalSrc
        mark: 123
    - name: envoy.filters.http.router
      typed_config: {}
