.. _config_http_filters_squash:

Squash
======

Squash 是一个 HTTP 过滤器，可以让 Envoy 和 Squash 微服务调试器能够进行集成。代码地址：https://github.com/solo-io/squash，API 文档地址：https://squash.solo.io/。

概览
-----

此过滤器的主要用例在服务网格中，其中 Envoy 是以 sidecar 的方式被部署的。一旦有测试标记的请求进入网格，Squash Envoy 过滤器就会将请求在集群中的 ‘location’ 报告给 Squash 服务器 - 因为 Envoy sidecar 和应用容器之间是 1-1 映射的，Squash 服务器能够找到且把调试器附着到应用容器上。Squash 过滤器还能够保留请求，直到调试器被附着上（或者发生了超时）。这能够让开发人员（通过 Squash）在请求到达应用程序代码之前且对集群不做任何变更的情况下，将原生调试器附着到那些处理请求的容器上。

配置
------

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.squash.v3.Squash>`
* 此过滤器的名称应该配置为 *envoy.filters.http.squash*。

工作原理
----------

当 Squash 过滤器遇到一个包含有 ‘x-squash-debug’ 头部的请求时，它将会：

1. 延迟传入请求。
2. 联系 Squash 服务器并请求创建 DebugAttachment。

   - 在 Squash 服务器侧，Squash 将会尝试把调试器附着到应用的 Envoy 代理上。成功后，它会把
     DebugAttachment 的状态改为附着。

3. 等待，直到 Squash 服务器把 DebugAttachment 对象的状态更新为附着（或者错误状态）。
4. 恢复传入请求。
