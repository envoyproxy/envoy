.. _why_is_my_route_not_found:

为什么找不到我的路由？
==========================

一旦你深入了解 Envoy 响应并且发现 Envoy 生成带有 "Sending local reply with details route_not_found" 消息的本地响应，那么下一个问题会为什么？

通常你可以查看你的路由配置和发送的头信息，并看到缺失的内容。
一个经常被忽略的问题就是 host:port 匹配。如果你的路由配置匹配到 www.host.com 域名，但是客户端发送请求到 ww.host.com:443 ，这并不会被匹配到。 

如果你遇到这样的问题，可以使用两种之一的方式解决。
第一个就是修改你的配置信息去匹配 host:port 对，从

.. code-block:: yaml

  domains:
    - "www.host.com"

修改为

.. code-block:: yaml

  domains:
    - "www.host.com"
    - "www.host.com:80"
    - "www.host.com:443"

另一个就是使用 :ref:`从主机头中剥离端口 <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.strip_matching_host_port>` 剥离端口。
但这并不是从不安全的请求中剥离 80 端口或者从安全的请求中剥离 443 端口。
它不仅在匹配路由时去除端口，还会在将发送到下游的主机地址进行修改不包含端口。

