.. _arch_overview_ip_transparency:

IP 透明性
===============

什么是 IP 透明
-----------------------

作为代理， Envoy 是一个 IP 端点： 它有自己的 IP 地址，不同于其他下游的请求的地址。因此，当 Envoy 建立到上游主机的
连接时，该连接的 IP 地址会不同于任何被代理的连接的 IP 地址。

有时候上游服务器或者网络需要知道连接的源 IP 地址，由于很多原因被称为 *下游远程地址*。一些例子包括：

* IP 地址用于组成身份的一部分，
* IP 地址用于强制执行网络策略，或者
* IP 地址包含在审计中

Envoy 支持多种方法把下游远程地址提供给上游主机。
这些技术在复杂性和可应用性上各不相同。

HTTP 头
------------

HTTP 头部可能在 :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` 头部字段中携带请求的原始 IP 地址。
上游服务器可以使用该头部确定下游远程地址。Envoy 也可以使用这个头部来选择 :ref:` 原始来源 HTTP 过滤器 <arch_overview_ip_transparency_original_src_http>`
使用的 IP 地址。

HTTP 头部方法有一些缺点：

* 它只适用于 HTTP。
* 它可能不被上游主机支持。
* 它需要仔细配置。

Proxy Protocol
--------------

`HAproxy Proxy Protocol <http://www.haproxy.org/download/1.9/doc/proxy-protocol.txt>`_ 定义了一种协议，
在主要 TCP 流之前，通讯 TCP 连接的元数据。该元数据包括来源 IP。Envoy 支持使用 :ref:`Proxy Protocol 过滤器 <config_listener_filters_proxy_protocol>`
消费该信息，这可以用于恢复下游远程地址传播到 :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` 头部字段。
它也可以和 :ref:` 原始来源监听过滤器 <arch_overview_ip_transparency_original_src_listener>` 连接使用。最后，
Enovy 支持使用 :ref:`Proxy Protocol 传输套接字 <extension_envoy.transport_sockets.upstream_proxy_protocol>` 生成该头部。

这里有一个创建套接字的样例配置：

.. code-block:: yaml

    clusters:
    - name: service1
      connect_timeout: 0.25s
      type: strict_dns
      lb_policy: round_robin
      transport_socket:
        name: envoy.transport_sockets.upstream_proxy_protocol
        typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.proxy_protocol.v3.ProxyProtocolUpstreamTransport
        config:
          version: V1
        transport_socket:
          name: envoy.transport_sockets.raw_buffer
      ...

注意：如果你正在封装 TLS 套接字，这个头部会在 TLS 握手发生前被发送。

一些 Proxy Protocol 的缺点：

* 它只支持 TCP 协议。
* 它需要上游主机支持。

.. _arch_overview_ip_transparency_original_src_listener:

原始来源监听过滤器
-------------------------------

在可控的部署中，有可能通过使用 :ref:` 原始来源监听过滤器 <config_listener_filters_original_src>` 在一个上游
连接中复制下游远程地址。元数据不会加入到上游请求或者流中。相反，上游连接会用下游主机地址作为来源地址来建立。
过滤器可以和任何上游协议或者主机一起工作。然而，它需要相当复杂的配置，并且它不支持所有的部署，因为路由的限制。

原始来源监听过滤器的一些缺点：

* 它需要 Envoy 可以访问下游远程地址。
* 它的配置相当复杂。
* 由于连接池的限制，它可能会带来轻微的性能下降。

.. _arch_overview_ip_transparency_original_src_http:

原始来源 HTTP 过滤器
---------------------------

在可控的部署中，有可能通过使用 :ref:` 原始来源 HTTP 过滤器 <config_http_filters_original_src>` 在一个上游
连接中复制下游远程地址。 该过滤器操作上和 :ref:` 原始来源监听过滤 <arch_overview_ip_transparency_original_src_listener>`
非常相似。 他们的主要不同是，它可以通过 HTTP 头部推断出原始来源地址，这对于单个下游连接携带多个来自不同
原始来源地址的 HTTP 请求的情况非常重要。前端直连 sidecar 代理的部署是应用的样例。

该过滤器可以和任何上游的 HTTP 主机一起工作。尽管如此，它需要相当复杂的配置，并且它由于路由限制可能不支持所有的部署。

原始来源 HTTP 过滤器的一些缺点:

* 它需要 Envoy 适当的配置，以便从 :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` 头部
  提取下游远程地址。
* 它的配置相当复杂。
* 它可能会因为连接池的限制产生轻微的性能冲击。
