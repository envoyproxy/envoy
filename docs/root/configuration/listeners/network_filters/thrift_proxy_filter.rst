.. _config_network_filters_thrift_proxy:

Thrift 代理
============

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.v3.ThriftProxy>`
* 过滤器应该以名称 *envoy.filters.network.thrift_proxy* 来配置。

集群协议选项
--------------

到上游主机的 Thrift 连接可以通过向适当的集群中的 :ref:`extension_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.typed_extension_protocol_options>` 添加一个由 `envoy.filters.network.thrift_proxy` 作为键的记录来配置。
:ref:`ThriftProtocolOptions<envoy_v3_api_msg_extensions.filters.network.thrift_proxy.v3.ThriftProtocolOptions>` 消息中描述了可用的选项。

Thrift 请求元数据
-------------------

:ref:`头传输 <envoy_v3_api_enum_value_extensions.filters.network.thrift_proxy.v3.TransportType.HEADER>` 和 :ref:`TWITTER 协议<envoy_v3_api_enum_value_extensions.filters.network.thrift_proxy.v3.ProtocolType.TWITTER>` 支持元数据。
特别地 `头传输 <https://github.com/apache/thrift/blob/master/doc/specs/HeaderFormat.md>`_ 支持键值（key/value）对信息和 Twitter 协议传输支持 `追踪和请求上下文数据 <https://github.com/twitter/finagle/blob/master/finagle-thrift/src/main/thrift/tracing.thrift>`_ 。

头传输元数据
~~~~~~~~~~~~~~~~~~~

头传输键值对可以作为路由 :ref:`头 <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.RouteMatch.headers>` 。

Twitter 协议元数据
~~~~~~~~~~~~~~~~~~~~

Twitter 协议请求上下文被转换为可作为 :ref:`头 <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.RouteMatch.headers>` 路由的头。此外，以下字段都可以作为头：

客户端标识
    ClientId 的 `name` 字段成为（嵌套在 RequestHeader 的 `client_id` 中） `:client-id` 头。

目的地
    RequestHeader 的 `dest` 字段成为 `:dest` 头。

授权（Delegations）
    来自 RequestHeader `delegations` 字段中的每个 Delegation 作为头被添加，头名称的前缀是 `:d:` 后面紧跟 Delegation 的 `src` 。
    值是 Delegation 的 `dst` 字段。

元数据互操作性
~~~~~~~~~~~~~~~~

当下游和上游连接之间发生转换时，可用于路由的请求元数据（见上下文）将在连接格式之间自动转换。Twitter 协议请求上下文、客户端标识、目的地和 delegations 被表示如上所述的为头传输键值对。
类似地，头传输键值对可以被表示为 Twitter 协议请求上下文，除非他们与上面描述的特殊名称匹配。例如，带有信息键 “:client-id” 的下游头传输请求被转换为具有 ClientId 值的上游 Twitter 协议请求。
