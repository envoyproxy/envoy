.. _arch_overview_tracing:

追踪
=======

概览
--------
在大规模的面向服务的架构中，分布式追踪能让开发者得到调用流程的可视化展示。在了解序列化、并行情况和延迟来源的时候，
可视化就显得格外重要。Envoy 支持三个系统范围内追踪的特性：

* **生成 Request ID**: Envoy 会在需要的时候生成 UUIDs，并且填充名为 :ref:`config_http_conn_man_headers_x-request-id` 的 HTTP header。
    应用可以转发这个 x-request-id Header 来做到统一的记录和跟踪。这个行为可以通过用一个 extension 在每个基于 :ref:`HTTP connection manager<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.request_id_extension>` 上配置。
* **客户端跟踪 ID 连接**: :ref:`config_http_conn_man_headers_x-client-trace-id` header 能够把不信任的请求 ID 连接到受信任的内部 :ref:`config_http_conn_man_headers_x-request-id` header 上。
* **集成外部跟踪服务**: Envoy 支持插件式的外部可视化跟踪提供者，有两种不同的类型：

  - 基于代码级别的外部跟踪服务，像 `LightStep <https://lightstep.com/>`_, `Zipkin <https://zipkin.io/>`_ 或者 任何兼容 Zipkin 的后端服务（例如：`Jaeger <https://github.com/jaegertracing/>`_），
    还有 `Datadog <https://datadoghq.com>`_。
  - External tracers which are part of the Envoy code base, like `LightStep <https://lightstep.com/>`_,
    `Zipkin <https://zipkin.io/>`_  or any Zipkin compatible backends (e.g. `Jaeger <https://github.com/jaegertracing/>`_), and
    `Datadog <https://datadoghq.com>`_.
  - 外部追踪服务作为第三方库的插件，像 `Instana <https://www.instana.com/blog/monitoring-envoy-proxy-microservices/>`_。

支持添加其它各种类型的的追踪服务也不是什么难事！

如何初始化一个跟踪
-----------------------
处理请求的 HTTP 管理器必须设置 :ref:`tracing
<envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.Tracing>` 对象。有多种不同的方式可以初始化跟踪：

* 外部客户端，使用 :ref:`config_http_conn_man_headers_x-client-trace-id` header。
* 内部服务，使用 :ref:`config_http_conn_man_headers_x-envoy-force-trace` header。
* 通过 :ref:`random_sampling <config_http_conn_man_runtime_random_sampling>` 运行时设置来进行随机采样。

路由过滤器可以使用 :ref:`start_child_span <envoy_v3_api_field_extensions.filters.http.router.v3.Router.start_child_span>` 来为 egress 调用创建下级 span。

跟踪上下文的传递
-------------------------
Envoy 提供了报告网格内服务间通信跟踪信息的能力。然而一个调用流中，会有多个代理服务器生成的跟踪信息碎片，
跟踪服务必须在出入站的请求中传播上下文信息，才能拼出完整的跟踪信息。

不管使用哪种跟踪方式，服务都应该传递 :ref:`config_http_conn_man_headers_x-request-id` 来开启调用服务的相关性记录。

为了理解 Span（逻辑工作单元）之间的父子关系，跟踪服务自身也需要更多的上下文信息。这种需要可以
通过在跟踪服务自身中直接使用 LightStep（OpenTracing API）或者 Zipkin 跟踪器来完成，从而
在入站请求中提取跟踪上下文，然后把上下文信息注入到后续的出站请求中。这种方法还可以让服务能够
创建附加的 Span，用来描述服务内部完成的工作。这对端到端跟踪的检查是很有帮助的。

另外，跟踪上下文也可以由服务来完成手动传播：

* 如果使用了 LightStep 跟踪器，在发送 HTTP 请求到其他服务时，Envoy 依赖这个服务来传播 :ref:`config_http_conn_man_headers_x-ot-span-context` Header。

* 如果使用的是 Zipkin，Envoy 依赖这个服务传播
  B3 HTTP headers (
  :ref:`config_http_conn_man_headers_x-b3-traceid`,
  :ref:`config_http_conn_man_headers_x-b3-spanid`,
  :ref:`config_http_conn_man_headers_x-b3-parentspanid`,
  :ref:`config_http_conn_man_headers_x-b3-sampled`, and
  :ref:`config_http_conn_man_headers_x-b3-flags`)。 :ref:`config_http_conn_man_headers_x-b3-sampled`
  header 也可以由外部客户端在一个特定的请求中打开或者关闭追踪。另外，单个 :ref:`config_http_conn_man_headers_b3` header 
  传递格式是被支持的，这个格式是更加紧凑的格式。

* 如果使用的是 Datadog 追踪器, Envoy 依赖这个服务传递特定的 Datadog 
  HTTP headers (
  :ref:`config_http_conn_man_headers_x-datadog-trace-id`,
  :ref:`config_http_conn_man_headers_x-datadog-parent-id`,
  :ref:`config_http_conn_man_headers_x-datadog-sampling-priority`).

每条追踪中包含哪些数据
-----------------------------
一条端到端的跟踪会包含一个或者多个 Span。一个 Span 就是一个逻辑上的工作单元，具有一个启动时间和时长，
其中还会包含特定的 Metadata，每个 Envoy 生成的 Span 包含如下信息：

* 通过 :option:`--service-cluster` 设置的始发服务集群
* 请求的启动时间和持续时间。
* 使用 :option:`--service-node` 设置的始发服务主机。
* :ref:`config_http_conn_man_headers_downstream-service-cluster` header 设置的下游集群。
* HTTP 请求 URL, method, protocol 和 user-agent。
* 通过 :ref:`custom_tags
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.Tracing.custom_tags>` 设置的定制 tag。
* 上游集群名字和地址。
* HTTP 响应码。
* GRPC 响应状态和信息（如果有）。
* 当 HTTP 状态码是 5xx 或者 GRPC 状态不是 "OK" 的时候出现的错误 tag。
* 跟踪系统的特定信息。

Span 还有包含一个名称（或者操作），默认定义为被调用服务的服务名。当然，也可以在路由上使用 :ref:`envoy_v3_api_msg_config.route.v3.Decorator` 来进行自定义。还可以使用 :ref:`config_http_filters_router_x-envoy-decorator-operation` Header 来重写名称。

Envoy 自动发送 Span 给跟踪采集器。多个 Span 会被揉和在一起使用一些共同的信息，比如全局唯一的请求 ID :ref:`config_http_conn_man_headers_x-request-id` (LightStep) 或者跟踪 ID 配置（Zipkin 和 Datadog），当然这取决于跟踪采集器。

查看 :ref:`v3 API reference <envoy_v3_api_msg_config.trace.v3.Tracing>` 来获取更多的关于在 Envoy 中设置跟踪的信息。


Baggage
-----------------------------
Baggage 提供一种数据在整个跟踪过程中都可用的机制。尽管诸如标签之类的元数据被用来和带外收集器进行通信，但是 baggage 数据会被注入到实际的请求上下文中，
并且在请求持续的时间内对应用程序是可用的。这使得遍布在整个服务网格的元数据从请求的开始就是透明的，而无须依赖为了传递而修改特定的应用。
查看 `OpenTracing 文档 <https://opentracing.io/docs/overview/tags-logs-baggage/>`_ 来了解更多关于 baggage 的信息。

对于设置和获取 baggage，跟踪器提供商有着不同的水平：

* Lightstep (以及其它任何兼容 OpenTracing 的追踪器) 能够读/写 baggage
* Zipkin 支持至今还没有实现
* X-Ray 和 OpenCensus 不支持 baggage
