.. _arch_overview_mongo:

MongoDB
=======

Envoy 支持具有以下功能的网络级别 MongoDB 嗅探过滤器：
* MongoDB wire 格式的 BSON 解析器。
* 详细的 MongoDB 查询/操作统计信息，包括路由集群的计时和 scatter/multi-get 计数。
* 查询记录。
* 通过 $comment 参数做每个调用点的统计分析报告。
* 故障注入。

MongoDB 过滤器是 Envoy 的可扩展性和核心抽象能力的典范案例。在 Lyft 中，我们将这个过滤器应用在所有的应用以及数据库中。它提供了重要的数据源，而这与应用程序平台和所使用的特定 MongoDB 的驱动程序无关。

MongoDB 代理过滤器 :ref:`配置参考 <config_network_filters_mongo_proxy>`。

