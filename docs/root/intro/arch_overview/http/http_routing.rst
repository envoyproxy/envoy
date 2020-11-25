.. _arch_overview_http_routing:

HTTP 路由
============

Envoy 包含一个可以被安装用来执行高级路由任务的 HTTP :ref:`路由过滤器 <config_http_filters_router>`。这对于处理边缘流量（传统的反向代理请求处理）以及构建服务间的 Envoy 网格
（通常通过基于 host/authority HTTP 请求头的路由访问特定的上游服务集群）都是有用的。Envoy 还可以配置为转发代理。在转发代理配置中，网格客户端可以通过适当地将其 http 代理配置为 Envoy 来参与。
在高层面上，路由器接收传入的 HTTP 请求，将其与上游集群匹配，获取连接到上游集群中一个主机的 :ref:`连接池 <arch_overview_conn_pool>`，然后转发该请求。 路由过滤器支持以下功能：

* 将 domains/authorities 映射到一组路由规则的虚拟主机。
* 前缀和精确路径匹配规则（ 支持 :ref:`大小写敏感 <envoy_v3_api_field_config.route.v3.RouteMatch.case_sensitive>` 和大小写不敏感）。当前不支持正则表达式/首字母匹配，主要是因为它很难以
  编程方式确定路由规则是否相互冲突。 因此，我们不建议在反向代理级别上进行正则表达式/首字母路由，但是将来可能会根据需求增加支持。
* 虚拟主机级别的 :ref:`TLS 重定向 <envoy_v3_api_field_config.route.v3.VirtualHost.require_tls>`。
* 路径级别的 :ref:`路径 <envoy_v3_api_field_config.route.v3.RedirectAction.path_redirect>`/:ref:`主机 <envoy_v3_api_field_config.route.v3.RedirectAction.host_redirect>` 重定向。 
* 在路由级别上 :ref:`直接（非代理）HTTP响应 <arch_overview_http_routing_direct_response>`。
* :ref:`显式主机重写 <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.host_rewrite>` 。
* 根据所选上游主机的 DNS 名称 :ref:`自动重写主机 <envoy_v3_api_field_config.route.v3.RouteAction.auto_host_rewrite>` 。
* :ref:`前缀重写 <envoy_v3_api_field_config.route.v3.RedirectAction.prefix_rewrite>` 。
* :ref:`使用正则表达式和捕获组进行路径重写 <envoy_v3_api_field_config.route.v3.RouteAction.regex_rewrite>` 。
* 通过 HTTP 请求头或路由配置指定的 :ref:`请求重试 <arch_overview_http_routing_retry>` 。
* 通过 :ref:`HTTP请求头 <config_http_filters_router_headers_consumed>` 或 :ref:`路由配置 <envoy_v3_api_field_config.route.v3.RouteAction.timeout>` 指定请求超时。
* :ref:`请求对冲 <arch_overview_http_routing_hedging>`，重启以响应请求（每次尝试）超时。
* 流量通过 :ref:`运行时值 <envoy_v3_api_field_config.route.v3.RouteMatch.runtime_fraction>` 从一个上游集群转移到另一个上游集群（请参阅 :ref:`流量转移/拆分 
  <config_http_conn_man_route_table_traffic_splitting>`）。
* 使用基于权重/百分比的路由跨多个上游集群进行流量拆分（请参阅 :ref:`流量转移/拆分 <config_http_conn_man_route_table_traffic_splitting_split>`）。
* 任意头匹配的 :ref:`路由规则 <envoy_v3_api_msg_config.route.v3.HeaderMatcher>` 。
* 虚拟集群规范。虚拟集群是在虚拟主机级别上指定的，Envoy 使用它在标准集群级别的基础上生成其他统计信息。虚拟集群可以使用正则表达式进行匹配。
* 基于 :ref:`优先级 <arch_overview_http_routing_priority>` 的路由。
* 基于 :ref:`哈希策略 <envoy_v3_api_field_config.route.v3.RouteAction.hash_policy>` 的路由。
* 非 tls 前向代理支持 :ref:`绝对 URL <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_protocol_options>` 。

.. _arch_overview_http_routing_route_scope:

路由作用域
-----------

作用域内路由使 Envoy 可以对域和路由规则的搜索空间施加约束。:ref:`路由作用域 <envoy_api_msg_ScopedRouteConfiguration>` 将关键字与 :ref:`路由表<arch_overview_http_routing_route_table>` 关联。
对于每个请求，HTTP 连接管理器会动态计算作用域键值来进行路由表的选择。可以在配置了 :ref:`v3 API参考 <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.OnDemand>` 的情况下按需加载与作用域
关联的 RouteConfiguration ，并且可以将 protobuf 中设置为 true 的按需加载。

作用域 RDS（SRDS）API 包含一组 :ref:`作用域 <envoy_v3_api_msg_config.route.v3.ScopedRouteConfiguration>` 资源，每个资源定义了独立的路由配置，同时一个 :ref:`ScopeKeyBuilder 
<envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.ScopedRoutes.ScopeKeyBuilder>` 定义了 Envoy 用于查找与每个请求相对应作用域的密钥构造算法。

例如，对于以下作用域的路由配置，Envoy 将查看“addr”请求头的值，并将请求头值通过“;”分割，并将键“x-foo-key”的第一个值用作作用域的键。如果“addr”请求头的值为“foo=1; x-foo-key=127.0.0.1; x-bar-key=1.1.1.1”，则将“ 127.0.0.1”计算后作为作用域关键字，以查找相应的路由配置。

.. code-block:: yaml

  name: scope_by_addr
  fragments:
    - header_value_extractor:
        name: Addr
        element_separator: ;
        element:
          key: x-foo-key
          separator: =

.. _arch_overview_http_routing_route_table:

为了使关键字与 :ref:`ScopedRouteConfiguration <envoy_v3_api_msg_config.route.v3.ScopedRouteConfiguration>` 匹配，计算键中的分片数量必须与 :ref:`ScopedRouteConfiguration 
<envoy_v3_api_msg_config.route.v3.ScopedRouteConfiguration>` 的数量相匹配。然后按顺序匹配片段。内置关键字中缺少片段（视为 NULL）会使请求无法匹配任何作用域，即找不到该请求的路由项。

路由表
-----------

HTTP 连接管理器的 :ref:`配置 <config_http_conn_man>` 中拥有所有已配置的 HTTP 过滤器使用的 :ref:`路由表 <envoy_v3_api_msg_config.route.v3.RouteConfiguration>`。尽管路由过滤器
是路由表的主要使用者，但其他过滤器也可以访问，以防它们要根据请求的最终目的地进行决策。例如内置的速率限制过滤器将查询路由表，以确定是否应基于该路由来调用全局速率限制服务。即使决定涉及随
机性（例如在运行时配置路由规则的情况下），连接管理器也要确保所有获取路由的调用对于特定请求都是稳定的。

.. _arch_overview_http_routing_retry:

重试语义
---------------

Envoy 允许在 :ref:`路由配置 <envoy_v3_api_field_config.route.v3.RouteAction.retry_policy>` 以及通过 :ref:`请求头 <config_http_filters_router_headers_consumed>` 的特定请求中配置重试。
有以下可用的配置：

* **最大重试次数**：Envoy 将继续重试任何次数。重试之间的时间间隔可以通过指数退避算法（默认），也可以基于上游服务器通过请求头（如果存在）的反馈来确定。此外 *所有重试都包含在整个请求超时内*。 
  这避免了由于大量重试而导致请求时间较长。
* **重试条件**：Envoy 可以根据应用要求在不同类型的条件下重试。例如网络故障、所有 5xx 响应代码、幂等 4xx 响应代码等。
* **重试限额**：Envoy 可以通过可重试的 :ref:`重试限额 <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.retry_budget>` 来限制活动请求的比例，以防止其造成流量的大幅增长。
* **主机选择重试插件**：可以将 Envoy 配置为在选择主机进行重试时将附加逻辑应用于主机选择逻辑。指定 :ref:`重试主机谓词 <envoy_v3_api_field_config.route.v3.RetryPolicy.retry_host_predicate>` 
  可以在选择某些主机时（例如在选择已尝试的主机时）重新尝试选择主机，通过配置 :ref:`重试优先级 <envoy_v3_api_field_config.route.v3.RetryPolicy.retry_priority>` 来调整选择重试优先级时
  使用的优​​先级负载。

请注意，存在 :ref:`x-envoy-overloaded<config_http_filters_router_x-envoy-overloaded_set>` 时 Envoy 重试请求。建议配置 :ref:`重试策略（首选） 
<envoy_api_field_cluster.CircuitBreakers.Thresholds.retry_budget>` 或将 :ref:`最大活动重试熔断器 <arch_overview_circuit_break>` 设置为适当的值以避免重试风暴。

.. _arch_overview_http_routing_hedging:

请求对冲
---------------

Envoy 支持请求对冲，可以通过指定 :ref:`对冲策略 <envoy_v3_api_msg_config.route.v3.HedgePolicy>` 来启用。这意味着 Envoy 将争用多个同时发生的上游请求，并将与第一个可接受的响应头相关联的响应返回到下游。
重试策略用于确定是否应返回响应或是否应等待更多响应。

当前对冲功能只能在响应请求超时来执行。这意味着将在不取消初始超时请求的情况下发出重试请求，并且将等待延迟响应。根据重试策略的第一个“good”响应将在下游返回。

该实现确保相同的上游请求不会重试两次。可能会发生请求超时导致得5xx响应并创建两个可重试事件。

.. _arch_overview_http_routing_priority:

优先路由
----------------

Envoy 在 :ref:`路由 <envoy_v3_api_msg_config.route.v3.Route>` 级别支持优先级路由。当前的优先级实现为每个优先级使用不同的 :ref:`连接池 <arch_overview_conn_pool>` 和 :ref:`熔断机制 <config_cluster_manager_cluster_circuit_breakers>` 。
这意味着即使对于 HTTP/2 请求，两个物理连接也将用于一个上游主机。将来 Envoy 可能会在单个连接上支持真正的 HTTP/2 优先级。

当前支持的优先级为 *default* 和 *high* 。

.. _arch_overview_http_routing_direct_response:

直接响应
----------------

Envoy 支持发送 "direct" 响应。这些是预先配置的 HTTP 响应，不需要代理到上游服务器。

有两种方法可以在路由中指定直接响应：

* 设置 :ref:`direct_response <envoy_v3_api_field_config.route.v3.Route.direct_response>` 字段。这适用于所有 HTTP 响应状态。
* 设置 :ref:`redirect <envoy_v3_api_field_config.route.v3.Route.redirect>` 字段。这仅适用于重定向响应状态，但简化了 *Location* 请求头的设置。

直接响应包含 HTTP 状态码和可选的正文。路由配置可以内联指定响应主体，也可以指定包含主体的文件的路径名。如果路由配置指定了文件路径名，则 Envoy 将在配置加载时读取文件并缓存内容。

.. 注意:

   如果指定了响应正文，则无论是内联还是以文件形式提供，其大小都不得超过4KB。Envoy 目前将整个请求体保留在内存中，因此4KB的限制是为了防止代理的内存占用过大。

如果已为路由或封闭的虚拟主机设置了 **response_headers_to_add**，Envoy 将在直接 HTTP 响应中包括指定的请求头。
