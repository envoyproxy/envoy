.. _deployment_type_double_proxy:

服务间、前端代理和双向代理
-------------------------------------------------

.. image:: /_static/double_proxy.svg
  :width: 70%

上图展示了一个 :ref:`前端代理 <deployment_type_front_proxy>` 与另一个 Envoy 前端代理组成的 *双向代理* 配置。双向代理
背后的想法是它能更有效地终止 TLS 和客户端连接，使其尽可能的接近用户（更短的 TLS 握手往返时间、更快的 TCP CWND 扩展、更低的丢包几率）。在双向代理中终止的连接会被多路复用到主数据中心中的 HTTP/2 长链接上。

在上图中，区域 1 中运行的前端 Envoy 代理通过 TLS 相互身份认证和固定证书与在区域 2 中运行的前端 Envoy 代理进行身份验证。这使
得在区域 2 中运行的前端 Envoy 代理可以信任普遍不可信的请求元素（如 HTTP header x-forwarded-for）。

配置模板
^^^^^^^^^^^^^^^^^^^^^^

源码包含一个双向代理的配置示例，这和 Lyft 在生产中使用的版本是非常类似的。更多信息可以查看 :ref:`这里 <install_ref_configs>`。
