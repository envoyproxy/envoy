.. _arch_overview_load_balancing_locality_weighted_lb:

区域性加权负载均衡
--------------------------------

在不同区域和地理位置之间进行权重分配的一种方法是使用 :ref:`LocalityLbEndpoints <envoy_v3_api_msg_config.endpoint.v3.LocalityLbEndpoints>` 消息中通过 EDS 提供的显示权重。这种方法与 :ref:`区域感知路由 <arch_overview_load_balancing_zone_aware_routing>` 是相互排斥的，因为在区域感知负载均衡的情况下，我们依靠管理服务器提供区域权重，而不是在区域感知路由中使用 Envoy 端内置的启发式算法。

当所有端点都可用时，我们会使用加权循环调度来挑选区域性，其中区域性权重被用于加权。当一个区域中的某些端点不可用时，我们会调整区域权重以反映这一点。与 :ref:`优先级 <arch_overview_load_balancing_priority_levels>` 处理一样，我们设定一个 :ref:`超额供给因数（Overprovisioning Factor）  <arch_overview_load_balancing_overprovisioning_factor>` （默认值为 1.4），这意味着当一个区域中只有少数端点不可用时，我们不执行任何权重调整。

假设一个简单的设置中，有 2 个区域 X 和 Y，其中 X 的区域权重为 1，Y 的区域权重为 2，L=Y 100% 可用，默认超额供给因数为 1.4。

+----------------+----------------+----------------+
| L=X 的健康端点 | L=X 的流量占比 | L=Y 的流量占比 |
+================+================+================+
| 100%           | 33%            | 67%            |
+----------------+----------------+----------------+
| 70%            | 33%            | 67%            |
+----------------+----------------+----------------+
| 69%            | 32%            | 68%            |
+----------------+----------------+----------------+
| 50%            | 26%            | 74%            |
+----------------+----------------+----------------+
| 25%            | 15%            | 85%            |
+----------------+----------------+----------------+
| 0%             | 0%             | 100%           |
+----------------+----------------+----------------+


我们通过伪代码来总结一下算法：

::

  availability(L_X) = 140 * available_X_upstreams / total_X_upstreams
  effective_weight(L_X) = locality_weight_X * min(100, availability(L_X))
  load to L_X = effective_weight(L_X) / Σ_c(effective_weight(L_c))

需要注意的是，区域加权选取是在优先级选取之后进行的。负载均衡器按照以下步骤执行选择：

1. 按 :ref:`优先级 <arch_overview_load_balancing_priority_levels>` 进行初选。
2. 从（1）的结果中按本节所述的算法选取符合条件的区域。
3. 从（2）的结果中按集群指定的负载均衡策略选取目标端点。

区域性加权负载均衡可以通过集群配置中的 :ref:`locality_weighted_lb_config <envoy_v3_api_field_config.cluster.v3.Cluster.CommonLbConfig.locality_weighted_lb_config>` 进行设置，其中通过 :ref:`load_balancing_weight <envoy_v3_api_field_config.endpoint.v3.LocalityLbEndpoints.load_balancing_weight>` 设置权重，并通过 :ref:`locality <envoy_v3_api_field_config.endpoint.v3.LocalityLbEndpoints.locality>` 中的 :ref:`LocalityLbEndpoints <envoy_v3_api_msg_config.endpoint.v3.LocalityLbEndpoints>` 确定上游主机的位置。

该功能与 :ref:`负载均衡器子集（Load Balancer Subsets） <arch_overview_load_balancer_subsets>` 不兼容，因为要将局部性的权重与单个子集的合理权重协调起来并不直接有效。
