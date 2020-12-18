.. _config_cluster_manager_cds:

集群发现服务（CDS）
=========================

集群发现服务 (Cluster Discovery Service - CDS) 是 Envoy 调用的一个可选 API，用来动态获取集群管理器成员。Envoy 将协调 API 响应，并根据需要添加、修改或删除已知集群。

.. note::

  不能通过 CDS API 修改或删除 Envoy 配置中静态定义的任何群集。

* :ref:`v3 CDS API <v2_grpc_streaming_endpoints>`

统计
----------

CDS 有一个以 *cluster_manager.cds.* 为根的 :ref:`统计树 <subscription_statistics>` 。
