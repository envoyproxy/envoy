.. _config_cluster_manager_cluster_circuit_breakers:

熔断
=====

* 熔断 :ref:`架构概览  <arch_overview_circuit_break>`。
* :ref:`v3 API 文档 <envoy_v3_api_msg_config.cluster.v3.CircuitBreakers>`。

下面是一个熔断器配置示例：

.. code-block:: yaml

  circuit_breakers:
    thresholds:
    - priority: "DEFAULT"
      max_requests: 75
      max_pending_requests: 35
      retry_budget:
        budget_percent:
          value: 25.0
        min_retry_concurrency: 10

运行时
-------

所有熔断设置均可在运行时根据群集名称针对所定义的优先级进行配置。它们遵循如下命名规则 ``circuit_breakers.<cluster_name>.<priority>.<setting>``。``cluster_name`` 是每个集群配置的名称字段，设置于 Envoy 的 :ref:`配置文件 <envoy_v3_api_field_config.cluster.v3.Cluster.name>` 中。可用的运行时设置将覆盖 Envoy 在配置文件中的设置。
