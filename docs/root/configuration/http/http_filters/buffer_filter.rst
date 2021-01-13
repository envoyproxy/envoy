.. _config_http_filters_buffer:

缓冲区
======

缓冲区过滤器被用于停止过滤器迭代以及等待完整缓冲的请求。这在很多情况下很有用，包括避免某些应用处理不完整的分块请求和高网络延迟。

如果启用缓冲区过滤器，在请求中没有 content-length 头，则缓冲区过滤器会添加这个头。
这一行为可以使用运行时属性 `envoy.reloadable_features.buffer_filter_populate_content_length` 来禁用。

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.buffer.v3.Buffer>`
* 此过滤器应该用名称 *envoy.filters.http.buffer* 来配置

按路由配置
-----------------------

通过在虚拟主机、路由以及加权集群上提供 :ref:`BufferPerRoute <envoy_v3_api_msg_extensions.filters.http.buffer.v3.BufferPerRoute>` 配置，
可以按路由来覆盖或禁用缓冲区过滤器配置。
