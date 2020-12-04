Envoy 是什么？
--------------

Envoy 是为面向大型现代服务架构而设计的 L7 代理和通信总线。该项目源于以下理念：

  *对于应用来说网络应该是透明的。当网络和应用出现故障时，应该非常容易定位问题发生的根源。*

事实上，实现上述的目标非常困难。Envoy 试图通过提供以下高级功能来实现这一目标：

**进程外架构**：Envoy 是一个独立进程，伴随每个应用服务运行。所有的 Envoy 形成一个透明的通信网格，每个应用与 localhost 收发信息，对网络的拓扑结构无感知。在服务间通信的场景下，进程外架构对比传统软件库的方式有两大优势：

  * Envoy 适用于任何应用编程语言。Envoy 部署可以在 Java、C++、Go、PHP、Python 等不同语言编写的应用之间形成一个网格。在面向服务架构中，使用多种应用框架和编程语言变得越来越普遍。Envoy 弥合了它们之间的差异。

  * 任何与面向大型服务架构打过交道的人都知道部署和升级软件库非常的痛苦。Envoy 可以透明地在整个基础架构上快速部署和升级。

**L3/L4 filter 架构**：Envoy 的核心是一个 L3/L4 网络代理。可插拔的 :ref:`filter <arch_overview_network_filters>` 链机制允许开发 filter 来执行不同 TCP/UDP 代理任务并将其插入到主服务中。现已有多个支持各种任务的 filter，如原始的 :ref:`TCP 代理 <arch_overview_tcp_proxy>`、:ref:`UDP 代理 <arch_overview_udp_proxy>`、:ref:`HTTP 代理 <arch_overview_http_conn_man>`、:ref:`TLS 客户端证书认证 <arch_overview_ssl_auth_filter>`、:ref:`Redis <arch_overview_redis>`、:ref:`MongoDB <arch_overview_mongo>` 和 :ref:`Postgres <arch_overview_postgres>` 等。

**HTTP L7 filter 架构**：HTTP 是现代应用架构中的关键组件，Envoy :ref:`支持 <arch_overview_http_filters>` 额外的 HTTP L7 filter 层。可以将 HTTP filter 插入执行不同任务的 HTTP 连接管理子系统中，如 :ref:`缓存 <config_http_filters_buffer>`、:ref:`限速 <arch_overview_global_rate_limit>`、:ref:`路由/转发 <arch_overview_http_routing>`、嗅探 Amazon 的 :ref:`DynamoDB <arch_overview_dynamo>` 等。

**顶级 HTTP/2 支持**：当以 HTTP 模式运行时，Envoy 同时 :ref:`支持 <arch_overview_http_protocols>` HTTP/1.1 和 HTTP/2。Envoy 可以作为 HTTP/1.1 和 HTTP/2 之间的双向透明代理。这意味着任意 HTTP/1.1 和 HTTP/2 客户端和目标服务器的组合都可以桥接在一起。建议配置所有服务之间的 Envoy 使用 HTTP/2 来创建持久连接的网格，以便可以实现请求和响应的多路复用。

**HTTP L7 路由**：当以 HTTP 模式运行时，Envoy 支持一种 :ref:`路由 <arch_overview_http_routing>` 子系统，能够根据路径、权限、内容类型、:ref:`运行时 <arch_overview_runtime>` 参数值等对请求进行路由和重定向。这项功能在将 Envoy 用作前端/边缘代理时非常有用，同时在构建服务网格时也会使用此功能。

**gRPC 支持**：`gRPC <https://www.grpc.io/>`_ 是一个来自 Google 的 RPC 框架，它使用 HTTP/2 作为底层多路复用传输协议。Envoy :ref:`支持 <arch_overview_grpc>` 被 gRPC 请求和响应的作为路由和负载均衡底层的所有 HTTP/2 功能。这两个系统是非常互补的。

**服务发现和动态配置**：Envoy 可以选择使用一组分层的 :ref:`动态配置 API <arch_overview_dynamic_config>` 来实现集中化管理。这些层为 Envoy 提供了以下内容的动态更新：后端集群内的主机、后端集群本身、HTTP 路由、监听套接字和加密材料。对于更简单的部署，可以 :ref:`通过 DNS 解析 <arch_overview_service_discovery_types_strict_dns>` （甚至 :ref:`完全跳过 <arch_overview_service_discovery_types_static>` ）发现后端主机，使用静态配置文件将替代深层配置。

**健康检查**：:ref:`推荐 <arch_overview_service_discovery_eventually_consistent>` 使用将服务发现视为最终一致的过程的方式来建立 Envoy 网格。Envoy 包含了一个 :ref:`健康检查 <arch_overview_health_checking>`，可以选择对上游服务集群执行主动健康检查。然后，Envoy 联合使用服务发现和健康检查信息来确定健康的负载均衡目标。Envoy 还通过 :ref:`异常检查 <arch_overview_outlier_detection>` 子系统支持被动健康检查。

**高级负载均衡**：`负载均衡 <arch_overview_load_balancing>` 是分布式系统中不同组件之间的一个复杂问题。由于 Envoy 是一个独立代理而不是软件库，因此可以独立实现高级负载均衡以供任何应用程序访问。目前，Envoy 支持 :ref:`自动重试 <arch_overview_http_routing_retry>`、:ref:`熔断 <arch_overview_circuit_break>`、通过外部速率限制服务的 :ref:`全局限速 <arch_overview_global_rate_limit>`、:ref:`请求映射 <envoy_v3_api_msg_config.route.v3.RouteAction.RequestMirrorPolicy>`
和 :ref:`异常检测 <arch_overview_outlier_detection>`。未来还计划支持请求竞争。

**前端/边缘代理支持**：在边缘使用相同的软件大有好处（可观察性、管理、相同的服务发现和负载均衡算法等）。Envoy 包含足够多的功能，可作为大多数现代 Web 应用程序的边缘代理。包括 :ref:`TLS <arch_overview_ssl>` 终止、HTTP/1.1 和 HTTP/2 :ref:`支持 <arch_overview_http_protocols>`，以及 HTTP L7 :ref:`路由 <arch_overview_http_routing>`。

**最佳的可观察性**：如上所述，Envoy 的主要目标是让网络透明化。然而，在网络层面和应用层面都有可能出现问题。Envoy 包含对所有子系统的强大 :ref:`统计 <arch_overview_statistics>` 支持。目前支持 `statsd <https://github.com/etsy/statsd>`_（和兼容程序）作为统计信息接收器，但是插入不同的接收器并不困难。统计信息也可以通过 :ref:`管理 <operations_admin_interface>` 端口查看。通过第三方提供商，Envoy 还支持分布式 :ref:`追踪 <arch_overview_tracing>`。
