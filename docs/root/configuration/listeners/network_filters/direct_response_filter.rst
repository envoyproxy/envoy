.. _config_network_filters_direct_response:

直接响应
==========

直接响应过滤器是一个不重要的网络过滤器，使用一个可选的预设响应立即响应新的下游连接。例如，它可以用作过滤器链中的末端过滤器来收集阻塞流量的遥测数据。过滤器应该以名称 *envoy.filters.network.direct_response* 来配置。

* :ref:`v3 API 参考 <envoy_v3_api_field_config.listener.v3.Filter.name>`
