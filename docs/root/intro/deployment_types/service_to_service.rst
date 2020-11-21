.. _deployment_type_service_to_service:

仅服务间部署
-----------------------

.. image:: /_static/service_to_service.svg
  :width: 60%

上图展示了一种 Envoy 最简单的部署方式，将 Envoy 作为通信总线代理面向服务的体系结构（SOA）应用内部的所有流量。
在这个场景中，Envoy 暴露了几个用于本地通讯和服务间通讯的监听器。

服务间 Egress 监听器
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

应用使用这个端口与基础设施中的其他服务进行通讯。例如 *http://localhost:9001* 。 HTTP 和 gRPC 请求通过
HTTP/1.1 *host* header 或者 HTTP/2 *:authority* header 来标明要访问的远程集群。 Envoy 根据配置中的详细
规则来处理服务发现、负载均衡和限流等操作。服务只需要关心本地的 Envoy ，而不需要关心网络拓扑结构，无论它们是在开发还是生产中运行。

这个监听器根据应用程序的不同可以同时支持 HTTP/1.1 或者 HTTP/2 。

.. image:: /_static/service_to_service_egress_listener.svg
  :width: 40%

.. _deployment_type_service_to_service_ingress:

服务间 Ingress 监听器
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

这是远程 Envoy 想与本地 Envoy 通讯时使用的端口。例如 *http://servicename:9211* 。Envoy 将所有从这个端口传入的请求路由到本地服务配置的其他端口上。根据应用程序或负载均衡的需要，可能涉及多个应用程序端口（例如，如果服务需要同时监听 HTTP 端口和 gRPC 端口）。本地 Envoy 根据需要执行缓冲、熔断等操作。

不管应用程序与本地 Envoy 通讯时使用 HTTP/1.1 还是 HTTP/2，Envoy 实例间的通讯都默认使用 HTTP/2。因为 HTTP/2 通过长连接和显式重置（发送 RST_STREAM 类型的 frame）等机制可以提供更好的性能。

.. image:: /_static/service_to_service_ingress_listener.svg
  :width: 55%


可选的外部服务 Egress 监听器
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

通常，对于本地服务想要通讯的每个外部服务都会显式声明一个 Egress 端口。这样做是因为一些外部服务的 SDK 不好重写 *host* header ，从而无法实现标准的反向代理行为。例如，可以为访问 DynamoDB 的连接分配 *http://localhost:9250* 端口。对于一些外部服务使用 *host* 路由，而其他服务使用专用的本地端口，我们建议对所有外部服务都是用本地端口路由。

服务发现集成
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

建议服务间的 Envoy 配置使用一个额外的服务来实现所有集群的服务发现，它会向 Envoy 提供最详尽的信息来做负载均衡和统计数据采集等。

配置模板
^^^^^^^^^^^^^^^^^^^^^^

源代码发行版包含一个 :ref:`服务间的配置示例 <install_ref_configs>` ，这与 Lyft 在生产中运行的版本非常相似。
