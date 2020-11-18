.. _arch_overview_grpc:

gRPC
====

`gRPC <https://www.grpc.io/>`_ 是来自 Google 的 RPC 框架。它使用 protocol buffers 作为底层序列化/IDL（接口描述语言）格式。在传输层，它使用 HTTP/2 进行请求/响应复用。Envoy在传输层和应用层都提供对 gRPC 一流的支持：

* gRPC 使用 HTTP/2 trailers 来传送请求状态。 Envoy 是能够正确支持 HTTP/2 trailers 的少数几个 HTTP 代理之一，因此也是少数可以传输 gRPC 请求和响应的代理之一。
* 某些语言的 gRPC 运行时相对不成熟。有关 gRPC 引入更多语言的过滤器的概述请参见 :ref:`桥接过滤器 <arch_overview_grpc_bridging>`。
* gRPC-Web 由一个指定的 :ref:`过滤器 <config_http_filters_grpc_web>` 支持，该过滤器允许 gRPC-Web 客户端通过 HTTP/1.1 向 Envoy 发送请求并代理到 gRPC 服务器。目前相关团队正在积极开发中，预计它将成为 gRPC :ref:`桥接过滤器 <config_http_filters_grpc_bridge>` 的后续产品。
* gRPC-JSON 转码器由一个指定的 :ref:`过滤器 <config_http_filters_grpc_json_transcoder>` 支持，该过滤器允许 RESTful JSON API 客户端通过 HTTP 向 Envoy 发送请求并获取代理到 gRPC 服务。

.. _arch_overview_grpc_bridging:

gRPC 桥接
-------------

Envoy 支持两种 gRPC 桥接过滤器:

* :ref:`gRPC HTTP/1.1 桥接过滤器 <config_http_filters_grpc_bridge>` 允许将 gRPC 请求通过 HTTP/1.1 发送到 Envoy。然后，Envoy 将请求转换为 HTTP/2 传输到目标服务器。响应则被转换回 HTTP/1.1。安装后，桥接过滤器除了收集标准的全局 HTTP 统计数据外，还收集每个 RPC 的统计数据。
* :ref:`gRPC HTTP/1.1 反向桥接过滤器 <config_http_filters_grpc_http1_reverse_bridge>` 允许将 gRPC 请求发送到 Envoy，然后在发送到上游时转换为 HTTP/1.1。在发送到下游时，响应又被转换回 gRPC。该过滤器还可以选择性地管理 gRPC 帧头，这样一来，上游就完全不用知道 gRPC 了。

.. _arch_overview_grpc_services:

gRPC 服务
-------------

Envoy 除了在数据平面上代理 gRPC 外，在控制平面上也使用了 gRPC，在控制平面上，它从中 :ref:`获取管理服务器的配置 <config_overview>` 以及过滤器的配置，例如将其用于 :ref:`限流 <config_http_filters_rate_limit>` 或授权检查。我们称之为 *gRPC 服务*。

当指定 gRPC 服务时，必须指定使用 :ref:`Envoy gRPC 客户端 <envoy_v3_api_field_config.core.v3.GrpcService.envoy_grpc>` 或 :ref:`Google C++ gRPC 客户端 <envoy_v3_api_field_config.core.v3.GrpcService.google_grpc>`。 我们在下面讨论这种选择的权衡。

Envoy gRPC 客户端是 gRPC 的最小自定义实现，它使用 Envoy 的 HTTP/2 上游连接管理。服务被指定为常规的 Envoy :ref:`集群 <arch_overview_cluster_manager>`，并定期处理 :ref:`超时、重试 <arch_overview_http_conn_man>`、端点 :ref:`发现 <arch_overview_dynamic_config_eds>`、:ref:`负载均衡/故障转移 <arch_overview_load_balancing>`、负载报告、:ref:`熔断 <arch_overview_circuit_break>`、:ref:`健康检查 <arch_overview_health_checking>`、:ref:`异常检测 <arch_overview_outlier_detection>`。它们与 Envoy 的数据面共享相同的 :ref:`连接池 <arch_overview_conn_pool>` 机制。同样，集群 :ref:`统计信息 <arch_overview_statistics>` 用于 gRPC 服务。由于客户端是简化版的 gRPC 实现，因此不包括诸如 `OAuth2 <https://oauth.net/2/>`_ 或 `gRPC-LB <https://grpc.io/blog/loadbalancing>`_ 之类的高级 gRPC 功能后备。

Google C++ gRPC 客户端的实现是基于 Google 在 https://github.com/grpc/grpc 上提供的 gRPC 参考。它提供了 Envoy gRPC 客户端中缺少的高级 gRPC 功能。Google C++ gRPC 客户端独立于 Envoy 的集群管理，执行自己的负载平衡、重试、超时、端点管理等。Google C ++ gRPC客户端还支持 `自定义身份认证插件 <https://grpc.io/docs/guides/auth.html#extending-grpc-to-support-other-authentication-mechanisms>`_。

在大多数情况下，当你不需要 Google C++ gRPC 客户端的高级功能时，建议使用 Envoy gRPC 客户端。这使得配置和监控更加简单。如果 Envoy gRPC 客户端中缺少你所需要的功能，则应该使用 Google C++ gRPC 客户端。

