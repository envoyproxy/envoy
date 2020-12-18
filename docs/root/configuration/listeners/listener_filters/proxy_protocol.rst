.. _config_listener_filters_proxy_protocol:

代理协议
=========

此监听器过滤器添加了对 `HAProxy 代理协议 <https://www.haproxy.org/download/1.9/doc/proxy-protocol.txt>`_ 的支持。

在这种模式下，假定下游连接来自于一个将原始信息（IP、PORT）置于连接字符中（connection-string）的代理。随后 Envoy 会提取那些信息且将它们当作远端地址来使用。

在代理协议 v2 中，存在可选的扩展（TLV）标记概念。如果在过滤器的配置中添加了 TLV 的类型，则 TLV 将作为带有用户指定密钥的动态元数据被发出。


这种实现同时支持版本 v1 和版本 v2，它会基于每一个连接（per-connection）来自动决定使用两个版本中的哪个。注意：如果开启了过滤器，代理协议必须存在于连接中（版本 1 或者版本 2），因为标准不允许通过解析来确定它是否存在。


如果有协议错误或者不支持的地址簇（比如 AF_UNIX），则连接将被关闭且抛出错误。

* :ref:`v3 API reference <envoy_v3_api_field_config.listener.v3.Filter.name>`
* This filter should be configured with the name *envoy.filters.listener.proxy_protocol*.

统计
------

此过滤器会发出一下统计信息：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  downstream_cx_proxy_proto_error, Counter, 代理协议错误总数
