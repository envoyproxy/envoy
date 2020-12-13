HTTP/1.1 头部大小写转换
==============================

在处理 HTTP/1.1 请求时，Envoy 会将头部键名都改成小写字母，以统一标准化。这种行为是符合 HTTP/1.1 标准的，但在实践中可能会给迁移已有系统带来问题，因为它们的某些头部是大小写敏感的。

为了支持此类场景，Envoy 允许配置头部格式规格，并在序列化头部键名时生效。如果需要为响应头部配置格式，请在 :ref:`http_protocol_options <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_protocol_options>` 中配置。如果需要为上游请求头部配置格式，请在 :ref:`Cluster <envoy_v3_api_field_config.cluster.v3.Cluster.http_protocol_options>` 中配置。

