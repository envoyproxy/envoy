.. _config_http_filters_grpc_bridge:

gRPC HTTP/1.1 桥接
====================

* gRPC :ref:`架构概览 <arch_overview_grpc>`
* :ref:`v3 API 参考 <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.name>`
* 这个过滤器应该配置使用名称 *envoy.filters.http.grpc_http1_bridge*.

这是一个简单的过滤器，可以桥接不支持响应 trailers 的 HTTP/1.1 客户端到兼容 gRPC 服务器。通过执行以下操作：

* 发送请求时，过滤器将查看连接是否为 HTTP/1.1 协议以及请求内容类型是是否为 *application/grpc*。
* 如果是, 收到响应后，过滤器会进行缓存并等待 trailers，然后检查 *grpc-status* 状态码。如果状态码不为 0，过滤器会将 HTTP 响应码切换为 503。它还会复制 *grpc-status* 和 *grpc-message* trailers 放入响应头部中，以便客户端查看，如果客户端想要查看的话。
* 客户端应该发送 HTTP/1.1 请求，该请求转换为以下伪头部：

  * *\:method*: POST
  * *\:path*: <gRPC-METHOD-NAME>
  * *content-type*: application/grpc

* 该请求体应该为序列化的 grpc 请求体，即：

  * 1个字节的零（未压缩）。
  * 网络顺序 4 个字节的 proto 消息长度。
  * 序列化的原始消息。

* 因为此方式必须缓存响应以查找 *grpc-status* trailer，所以它只会与一元 gRPC API 一起使用。

此过滤器还会收集所有传输的 gRPC 请求的统计信息，即使这些请求是 HTTP/2 上的常规 gRPC 请求。

更多信息：连接协议格式在 `gRPC over HTTP/2 <https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md>`_.

.. attention::

   请注意，统计信息也通过 :ref:`gRPC stats filter
   <config_http_filters_grpc_stats>` 收集。将此过滤器用于 gRPC 自动测量记录已不推荐使用。

统计
----------

过滤器会在 *cluster.<route target cluster>.grpc.* 命名空间发出统计数据。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  <grpc service>.<grpc method>.success, Counter, 成功的服务/方法调用总数
  <grpc service>.<grpc method>.failure, Counter, 失败的服务/方法调用总数
  <grpc service>.<grpc method>.total, Counter, 服务/方法调用总数
