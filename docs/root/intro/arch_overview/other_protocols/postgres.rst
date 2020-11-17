.. _arch_overview_postgres:

Postgres
========

Envoy 支持网络级 Postgres 过滤器，以增加网络可观察性。通过使用 Postgres 代理，Envoy 可以解码 `Postgres 前端/后端协议`_，并从解码后的信息中收集统计信息。

Postgres 过滤器的主要目标是捕获运行时统计信息，而不会影响或对 Postgres 上游服务器产生任何负载，因为 Postgres 过滤器对于上游服务器是透明的。该过滤器当前提供以下功能：

* 解码非 SSL 流量，忽略 SSL 流量。
* 解码 session 信息。
* 捕获事务信息，包括提交和回滚。
* 显示不同类型语句（INSERT、DELETE、UPDATE等）的计数器。计数器的更新是基于解码后端 CommandComplete 消息，而不是通过解码客户端发送的 SQL 语句。
* 计算前端、后端和未知消息。
* 识别后端响应的错误和通知

Postgres 过滤器解决了 Postgres 部署中的一个显著问题：收集这些信息要么给服务器带来额外的负载；要么需要从服务器上拉取查询元数据，或者有时需要外部组件或扩展。该过滤器可提供有价值的可观察性信息，而不会影响上游 Postgres 服务器的性能也不需要安装任何软件。

Postgres 代理过滤器 :ref:`配置参考 <config_network_filters_postgres_proxy>`。

.. _Postgres 前端/后端协议: https://www.postgresql.org/docs/current/protocol.html
