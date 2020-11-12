.. _arch_overview_mongo:

MongoDB
=======

Envoy 支持具有以下功能的网络级别 MongoDB 过滤器：
* MongoDB 格式的 BSON 解析器。
* 详细的 MongoDB 查询/操作统计信息，包括路由集群的计时和 scatter/multi-get 计数。
* 查询记录。
* 每个通过 $comment 查询参数的 callsite 统计信息。
* 故障注入。

MongoDB 过滤器是 Envoy 的可扩展性和核心抽象的一个很好的例子。在 Lyft 中，我们在所有应用程序和数据库之间使用这个过滤器。它提供了对应用程序平台，及正在使用的特定 MongoDB 驱动程序无关的重要数据源。

MongoDB 代理过滤器 :ref:`配置参考 <config_network_filters_mongo_proxy>`。

