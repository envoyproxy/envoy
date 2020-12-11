.. _best_practices_level2:

将 Envoy 配置为一个二级代理
===========================

Envoy 是一个生产就绪的代理，然而，默认配置是为边缘代理用例定制的，当需要在多级部署中将 Envoy 当作一个“二级”代理使用时，需要做一些调整。

.. image:: /_static/multilevel_deployment.svg

**总而言之，如果你运行的是能够终止 HTTP/2 的 1.11.1 或者更高版本的二级 Envoy，我们强烈建议你通过将下游** :ref:`HTTP 消息选项验证 <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stream_error_on_invalid_http_message>` 的值**设置为 true** 来改变你二级 Envoy 的 HttpConnectionManager 配置。

如果有一个无效的 HTTP/2 请求且此选项没有进行设置，Envoy 会重置整个连接。作为 1.11.1 安全版本的一部分，这种行为已经发生了改变，目的是为了增强 Envoy 作为边缘代理时的安全性。不幸的是，由于无法保证边缘代理能够像 Envoy HTTP/2 协议栈一样严格遵守 HTTP/1 或 HTTP/2 的合规标准，这可能导致如下问题。如果一个客户端发送了一个通过了一级 Envoy 代理验证检查的请求，且请求通过 HTTP/2 连接（和其他潜在客户端共享）的多路复用进行转发，在二级 Envoy 上严格执行 HTTP/2 将会重置连接上的所有流，这会引起对于共享 L1-L2 连接的客户端会造成服务中断。如果一个恶意用户了解哪些流量可以绕过一级 Envoy 代理检查，他们就会把“恶意”流量引向一级 Envoy 代理，这就会对其他用户的流量造成严重的中断。

这种配置选项也会对无效的 HTTP/1.1 产生影响，但是影响不是那么严重。对于 Envoy L1 ，无效的 HTTP/1 请求也会导致连接重置。如果将选项设置为 true，则请求会被完全读取，连接将保持并且在后续的请求中被重用。
