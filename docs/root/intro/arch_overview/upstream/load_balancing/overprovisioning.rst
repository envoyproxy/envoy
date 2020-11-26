.. _arch_overview_load_balancing_overprovisioning_factor:

超额供给因数
-----------------------
超额供给因数是流量在优先级和区域纬度被 :ref:`超额供给的百分比值 <envoy_v3_api_field_config.endpoint.v3.ClusterLoadAssignment.Policy.overprovisioning_factor>`。在可用主机数量和超额供给因数的乘积降至 100 以下之前，Envoy 不会认为某个优先级或区域不可用。默认值是 140（以百分比为单位，即 140%），因此在可用端点的百分比低于 72% 之前，优先级或区域不会被视为不可用。
