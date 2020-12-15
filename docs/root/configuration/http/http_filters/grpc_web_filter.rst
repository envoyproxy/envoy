.. _config_http_filters_grpc_web:

gRPC-Web
========

* gRPC :ref:`架构概览 <arch_overview_grpc>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.grpc_web.v3.GrpcWeb>`
* 此过滤器应该配置名字为 *envoy.filters.http.grpc_web*.

这是一个允许 gRPC-Web 客户端和兼容 gRPC 的服务器桥接的过滤器，如下 https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-WEB.md.
