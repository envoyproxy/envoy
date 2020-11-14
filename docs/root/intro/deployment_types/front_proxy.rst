.. _deployment_type_front_proxy:

服务间外加前端代理
-----------------------------------

.. image:: /_static/front_proxy.svg

上图显示了作为 HTTP L7 边缘反向代理 Envoy 群集背后的 :ref:`服务到服务 <deployment_type_service_to_service>` 配置。反向代理提供以下功能：

* 终止 TLS。
* 支持 HTTP/1.1 和 HTTP/2。
* HTTP L7 全路由支持。
* Envoy 集群使用 :ref:`ingress 端口 <deployment_type_service_to_service_ingress>` 标准进行服务到服务的通信，并使用发现服务进行主机查找。因此，前端 Envoy 主机与任何其它 Envoy 主机的工作方式相同，除了其它 Envoy 主机不与别的服务搭配运行。这意味着它们用相同的方式操作，并且数据统计也相同。

配置模板
^^^^^^^^^^^^^^^^^^^^^^

源代码发行版包含一个与 Lyft 在生产环境中运行的版本非常相似的前端代理配置示例。浏览 :ref:`此处 <install_ref_configs>` 获取更多信息。
