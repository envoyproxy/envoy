.. _config_http_filters_grpc_http1_reverse_bridge:

gRPC HTTP/1.1 反向桥接
============================

* gRPC :ref:`架构概览 <arch_overview_grpc>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.grpc_http1_reverse_bridge.v3.FilterConfig>`
* 此过滤器应使用名称 *envoy.filters.http.grpc_http1_reverse_bridge* 进行配置。

这是一个过滤器，可以将传入的 gRPC 请求转换成 HTTP/1.1 请求，使得服务器不需要了解 HTTP/2 或者 gRPC 协议语义也可以处理该请求。

过滤器的工作原理:

* 检查传入请求的内容类型。如果是 gRPC 请求，则启用过滤器。
* 内容类型被修改为可配置的值。通过配置 ``application/grpc`` ，可以将其设置为空。
* 可以从请求体中剥离 gRPC 帧头部。当然如果是这样，头部中的内容长度也会被调整。
* 收到响应后，将会验证响应的内容类型，并且状态代码会被映射为一个 grpc 状态，这个 grpc 状态会被插入到响应的 trailer 。
* 响应体可以用 gRPC 帧报头作为前缀，如果需要的话会再一次调整头部中的内容长度。


由于已映射到 HTTP/1.1，因此此过滤器仅适用于单个连接的 gRPC 调用。

gRPC 帧头管理
----------------------------

通过设置 withhold_grpc_frame 选项, 过滤器将假定上游不了解任何 gRPC 的协议语义，并将请求体转换成简单的二进制编码，并对响应体执行反向转换。这样会简化了服务器侧处理这些请求，因为它们不再需要关注解析并生成 gRPC 格式的数据。

这是通过从请求体中剥离 gRPC 帧头部，同时在响应中注入 gRPC 帧头部来实现的。

如果未使用此功能，则上游必须准备好接收前缀带 gRPC 帧头部的HTTP/1.1请求，并使用 gRPC 响应格式进行响应。

如何禁用每个路由的 HTTP/1.1 反向网桥过滤器
-------------------------------------------------------

.. literalinclude:: _include/grpc-reverse-bridge-filter.yaml
    :language: yaml
