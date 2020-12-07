.. _arch_overview_data_sharing_between_filters:

过滤器之间的数据共享
============================

Envoy 提供将过滤器之间以及其他核心子系统之间的配置、元数据和每个请求/连接状态进行传输（例如访问日志记录）的机制。

静态
^^^^^^^^^^^^

静态状态是在加载配置时指定的任何不可变状态（例如通过 xDS）。静态状态分为三类：

元数据
--------

Envoy 配置的几个部分（例如监听器、路由、集群）包含可以在其中对任意键值对进行编码的 :ref:`元数据 <envoy_v3_api_msg_config.core.v3.Metadata>`。典型的模式是使用反向 DNS 格式的过滤器名称作为键，并在值中编码特定于过滤器的配置元数据。
此元数据是不可变的，并在所有请求/连接之间共享。此类配置元数据通常在引导期间或作为 xDS 的一部分提供。例如 HTTP 路由中的权重集群使用元数据来指示端点上与该权重集群相对应的标签。
另一个示例，负载均衡器子集使用与权重集群相对应的路由条目中的的元数据来选择集群中适当的端点。

类型化的元数据
--------------

这样的 :ref:`元数据 <envoy_v3_api_msg_config.core.v3.Metadata>` 是无类型的。在对元数据进行操作之前，调用者通常会将其转换为类型化的类对象。当重复执行转换时（例如对于每个请求流
或连接），转换的成本变得不可忽视。类型化的元数据通过允许过滤器为特定键注册一个一次性转换逻辑来解决此问题。传入的配置元数据（例如 xDS）在配置加载时转换为类对象。过滤器随后可以在运行时
（按请求或连接）获取元数据的类型化变体，从而消除了在请求/连接处理期间过滤器从 `ProtobufWkt::Struct` 反复转换为某个内部对象的需求。

例如如果一个过滤器希望在 `ClusterInfo` 中的键为 `xxx.service.policy` 的不透明元数据上具有便捷包装类，则可以注册一个继承自 `ClusterTypedMetadataFactory` 的工厂 
`ServicePolicyFactory`。工厂将 `ProtobufWkt::Struct` 转换为 `ServicePolicy` 类的实例（从 `FilterState::Object` 继承）。创建 Cluster 时，将创建并缓存关联的 ServicePolicy 实例。
注意，类型化的元数据不是元数据的新来源。它是从配置中指定的元数据中获取的。可以在访问记录器中配置一个 `FilterState::Object` 实现 `serializeAsProto` 方法来对其进行记录。

HTTP 每路由过滤器配置
-----------------------------------

在HTTP路由中，除了所有虚拟主机通用的全局过滤器配置之外， :ref:`typed_per_filter_config <envoy_v3_api_field_config.route.v3.VirtualHost.typed_per_filter_config>` 允许 
HTTP 过滤器具有特定于虚拟主机/路由的配置。此配置将转换并嵌入到路由表中。将特定于路由的过滤器配置视为全局配置的替代或增强功能，取决于HTTP过滤器的实现。例如 HTTP 故障过滤器使用
此技术来提供每个路由的故障配置。


`typed_per_filter_config` 是一个 `map<string, google.protobuf.Any>`。连接管理器会对此映射进行迭代，并调用过滤器工厂接口 `createRouteSpecificFilterConfigTyped` 来解析/验证
结构值，并将其转换为与路由本身一起存储的类型化类对象。然后 HTTP 过滤器可以在请求处理期间查询特定于路由的过滤器配置。

动态
^^^^^^^^^^^^^

动态的状态是由每个网络连接或每个 HTTP 流生成的。如果生成状态的过滤器需要，动态状态是可变的。

`Envoy::Network::Connection` 和 `Envoy::Http::Filter` 提供一个 `StreamInfo` 对象，该对象分别包含有关当前 TCP 连接和 HTTP 流（即 HTTP 请求/响应对）的信息。`StreamInfo` 包
含一组固定属性，作为类定义的一部分（例如 HTTP 协议、请求的服务器名称等）。另外它提供了在映射中存储类型化对象的功能（`map<string, FilterState::Object>`）。每个过滤器存储
的状态可以是一次写入（不可变）或多次写入（可变）。
