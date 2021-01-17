.. _operations_stats:

统计概览
=========

依赖于服务器的配置情况，Envoy 会输出大量的统计信息。这些统计可以在本地通过 :http:get:`/stats` 命令来查看，且通常会被发送到 :ref:`statsd 集群 <arch_overview_statistics>`。输出的统计信息记录在 :ref:`配置指南 <config>` 的相关章节中。在以下章节中可以找到一些几乎总是会用到，且更重要的统计信息：

* :ref:`HTTP 连接管理器 <config_http_conn_man_stats>`
* :ref:`上游集群 <config_cluster_manager_cluster_stats>`
