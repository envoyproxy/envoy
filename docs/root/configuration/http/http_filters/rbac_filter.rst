.. _config_http_filters_rbac:

基于角色的访问控制（RBAC）过滤器
==================================

RBAC 过滤器被用来对可识别的下游客户端（主体）做授权操作。这对于显式管理应用程序的调用者及保护它们免受非期望或禁止客户端的影响来说是非常有用的。过滤器支持基于连接属性（IP、端口、SSL 主题）以及传入请求的 HTTP 头部的 safe-list（允许）或者 block-list（拒绝）策略配置。此过滤器还支持在强制模式和影子模式下的策略，影子模式并不会影响真实的用户，它被用来对上生产之前的一系列新策略进行测试。


当请求被拒绝时，在 :ref:`RESPONSE_CODE_DETAILS<config_access_log_format_response_code_details>`
中会包含以 `rbac_access_denied_matched_policy[policy_name]` 为格式的，与引起请求拒绝相匹配的策略的名称（如果没有匹配到任何策略，policy_name 的值将会是 `none`），这有助于区分该拒绝行为是来自于 Envoy RBAC 过滤器还是上游后端。

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.rbac.v3.RBAC>`
* 此过滤器的名称应该被配置为 *envoy.filters.http.rbac*。

Per-Route 配置
---------------

通过在虚拟主机、路由或权重集群上提供 :ref:`RBACPerRoute <envoy_v3_api_msg_extensions.filters.http.rbac.v3.RBACPerRoute>` 配置，RBAC 过滤器的配置可以在 per-route 的基础上被覆盖或者禁用。

统计
----------

RBAC 过滤器的输出统计位于 *http.<stat_prefix>.rbac.* 命名空间中。:ref:`统计前缀 <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>` 来自于拥有 HTTP 连接的管理器。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  allowed, Counter, 允许访问的请求总数
  denied, Counter, 拒绝访问的请求总数
  shadow_allowed, Counter, 影子规则允许访问的请求总数
  shadow_denied, Counter, 影子规则拒绝访问的请求总数
  logged, Counter, 应该被记录的请求总数
  not_logged, Counter, 不应该被记录的请求总数

.. _config_http_filters_rbac_dynamic_metadata:

动态元数据
------------

RBAC 过滤器会发出以下动态元数据。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  shadow_effective_policy_id, string, 与该动作相匹配的有效的影子策略 ID（如果有的话）。
  shadow_engine_result, string, 影子规则的引擎结果（`允许` 或者 `拒绝`）。
  access_log_hint, boolean, 请求是否应该被记录。此元数据是共享的，且设置在关键的命名空间‘envoy.common’下。(查看 :ref:`共享的动态元数据 <shared_dynamic_metadata>`）。
