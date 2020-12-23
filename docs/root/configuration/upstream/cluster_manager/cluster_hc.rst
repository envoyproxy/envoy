.. _config_cluster_manager_cluster_hc:

健康检查
=========

* 健康检查 :ref:`架构预览 <arch_overview_health_checking>`。
* 如果一个集群配置了健康检查，会有额外的统计信息发出。这些信息会记录在 :ref:`这儿 <config_cluster_manager_cluster_stats>`。
* :ref:`v3 API 文档 <envoy_v3_api_msg_config.core.v3.HealthCheck>`。

.. _config_cluster_manager_cluster_hc_tcp_health_checking:

TCP 健康检查
-------------

匹配的执行类型如下：

.. code-block:: yaml


  tcp_health_check:
      send: {text: '0101'}
      receive: [{text: '02'}, {text: '03'}]

在每一个健康检查周期内，所有的 “send” 字节都会发送至目标服务器。

当检查响应时，"fuzzy" 匹配会被执行以确保找到每一个块，且按照指定的顺序，但不一定是连续的。因此，在上述的示例中，"04" 可以插在 "02" 和 "03" 的响应之间，且检查也将会通过。这么做是为了支持把不确定性数据，比如时间，插入响应中的协议。

较复杂的健康检查模式，诸如 send/receive/send/receive，现在还不支持。

如果 "receive" 是一个空数组，Envoy 将执行 "connect only" TCP 健康检查。在每一个检查周期内，Envoy 将尝试去连接上游主机，如果连接成功，则认定健康检查是成功的。每一个健康检查周期都会创建一个新连接。
