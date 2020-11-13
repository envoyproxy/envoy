.. _arch_overview_http_filters:

HTTP 过滤器
============

和 :ref:`网络层的过滤器 <arch_overview_network_filters>` 很像，Envoy 支持了链接管理中的 HTTP 层过滤器。
可以开发过滤器处理 HTTP 层的消息，而不用知道底层的物理协议（比如，HTTP/1.1、HTTP/2）或者多路复用功能。HTTP 层
的过滤器有 3 种类型：

* **解码器**: 解码过滤器用于链接管理器解码部分请求流（分请头，正文和 trailer 信息）。
* **编码器**: 编码过滤器用于链接管理器编码部响应求流（响应头，正文和 trailer 信息）。
* **解码器/编码器**: 解码/编码过滤器同时用于链接管理器解码部分请求流和编码部分相应流。

HTTP 层过滤器的 API 可以让过滤器处理消息而不用知道底层协议。像网络层过滤器一样，HTTP 过滤器能停止和继续
处理后续的过滤器。这样可以处理更复杂的场景，比如处理健康检查，调用一个限流服务，缓存，路由，给像 DynamoDB 这样的应用
收集流量统计信息等。HTTP 层的过滤器也能在单个请求流的上下文中共享状态（静态和动态）。参考这里可以了解更多：
:ref:`过滤器间的数据共享 <arch_overview_data_sharing_between_filters>`。Envoy 已经有了几个 HTTP 层的过滤器，
这些过滤器也记录在了架构概述中 :ref:`配置参考 <config_http_filters>` 。


.. _arch_overview_http_filters_ordering:

过滤器顺序
---------------

过滤器顺序在 :ref:`http_filters 字段 <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_filters>`
中很重要。如果按照如下顺序配置过滤器（假设这 3 个过滤器都是 解码/编码过滤器）：

.. code-block:: yaml

  http_filters:
    - A
    - B
    # 配置在最后的过滤器必须是一个终结过滤器，由 NamedHttpFilterConfigFactory::isTerminalFilter() 函数决定。
    # 这个过滤器很可能是路由过滤器。
    - C

链接管理器将会按照这样的顺序来调用 decoder filter：``A``、``B``、``C``。
另一方面，链接管理器将会以**相反**的顺序调用 encoder filter：``C``，``B``，``A``。