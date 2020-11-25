.. _arch_overview_load_balancing_types_original_destination:

原始目的地
--------------------

这是一种专用的负载均衡器，只能用于 :ref:`一个原始目的地集群 <arch_overview_service_discovery_types_original_destination>`。上游主机是根据下游连接元数据选择的，也就是，连接被打开到的地址，与传入连接被重定向到 Envoy 之前的目标地址相同。新的目的地址由负载均衡器按需添加到集群中，集群 :ref:`定期 <envoy_v3_api_field_config.cluster.v3.Cluster.cleanup_interval>` 清除集群中未使用的主机。原始目的地集群不能使用其他 :ref:`负载均衡策略 <envoy_v3_api_field_config.cluster.v3.Cluster.lb_policy>`。

.. _arch_overview_load_balancing_types_original_destination_request_header:

原始目的地主机请求头
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Envoy 也可以从一个名为 :ref:`x-envoy-original-dst-host <config_http_conn_man_headers_x-envoy-original-dst-host>` 的 HTTP 请求头中获取原始目的地。请注意，在这个请求头中应该传递完全解析的 IP 地址。例如，如果一个请求被路由到一个 IP 地址为 10.195.16.237、端口为 8888 的主机，那么请求头的值应该设置为 ``10.195.16.237:8888``。

