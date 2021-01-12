.. _config_network_filters_rbac:

基于角色的访问控制（RBAC）网络过滤器
=====================================

RBAC 网络过滤器用于授权已识别的下游客户端（主体）的操作（权限）。这对于显式管理应用程序的调用者和保护应用程序不受意外或禁止的代理的影响非常有用。过滤器支持基于连接属性（IP，端口，SSL 主题）的 safe-list（允许）或 block-list（拒绝）策略集的配置。过滤器还支持强制模式和影子模式下的策略。影子模式不会影响实际用户，它用于在将新策略推广到生产环境之前测试一组新策略是否有效。

当请求被拒绝时，:ref:`CONNECTION_TERMINATION_DETAILS<config_access_log_format_connection_termination_details>`
将以 `rbac_access_denied_matched_policy[policy_name]`
的格式包含导致拒绝的匹配策略的名称（如果没有匹配的策略，policy_name 将为 none），这有助于区分该拒绝行为是来自于 Envoy RBAC 过滤器还是上游后端。

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.network.rbac.v3.RBAC>`
* 过滤器名称应配置为 *envoy.filters.network.rbac*。

统计信息
--------

RBAC 网络过滤器在 *<stat_prefix>.rbac.* 命名空间中输出统计信息。

.. csv-table::
  :header: 名字, 类型, 描述
  :widths: 1, 1, 2

  allowed, Counter, 允许访问的请求总数
  denied, Counter, 被拒绝访问的请求总数
  shadow_allowed, Counter, 过滤器的影子规则允许访问的总请求
  shadow_denied, Counter, 过滤器的影子规则拒绝访问的请求总数
  logged, Counter, 应记录的请求总数
  not_logged, Counter, 不应记录的请求总数

.. _config_network_filters_rbac_dynamic_metadata:

动态元数据
----------

RBAC 过滤器产生以下动态元数据。

.. csv-table::
  :header: 名字, 类型, 描述
  :widths: 1, 1, 2

  shadow_effective_policy_id, string, 匹配行为有效的影子策略 ID（如果有的话)。
  shadow_engine_result, string, 影子规则的引擎结果（例如 `允许` 或者 `拒绝`）。
  access_log_hint, boolean, 是否应记录该请求。此元数据在关键命名空间 'envoy.common'（参见 :ref:`共享动态元数据<shared_dynamic_metadata>`）。
