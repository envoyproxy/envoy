.. _config_http_conn_man_headers:

HTTP 头部操作
========================

HTTP 连接管理器在解码过程中（接收请求时）和编码过程中（发送响应时）都会操作多项 HTTP 头部。

.. contents::
  :local:

.. _config_http_conn_man_headers_user-agent:

user-agent
----------

当启用 :ref:`add_user_agent<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.add_user_agent>` 选项后，连接管理器可以在解码时设定 *user-agent* 头部。该头部只有在尚未设置的情况下才能被修改。如果由连接管理器来设置该头部，则该值由 :option:`--service-cluster` 命令行选项来确定。

.. _config_http_conn_man_headers_server:

server
------

*server* 头部会在编码时被设置为 :ref:`server_name<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.server_name>` 选项中的值。

.. _config_http_conn_man_headers_x-client-trace-id:

x-client-trace-id
-----------------

如果外部客户端设置了该头部, Envoy 会将设置的 trace ID 与内部生成的 :ref:`config_http_conn_man_headers_x-request-id` 拼接起来。x-client-trace-id 需要保持全局的唯一性，我们推荐以 uuid4 的方式生成 ID。如果设置了此头部，它与 :ref:`config_http_conn_man_headers_x-envoy-force-trace` 有类似的效果。请参考 :ref:`tracing.client_enabled<config_http_conn_man_runtime_client_enabled>` 运行时设置。

.. _config_http_conn_man_headers_downstream-service-cluster:

x-envoy-downstream-service-cluster
----------------------------------

内部服务通常想知道哪个服务正在调用它们。外部调用的该头部会被清除，而内部调用则会在该头部中携带调用者的服务器集群信息。请注意在当前的实现中，它只是一个参考值，因为它虽然是由调用方设置的，但很容易被任意内部的实体篡改。将来，Envoy 将支持相互认证的 TLS 网格，从而让这个头部具有完整的安全性。类似 *user-agent*，该值由 :option:`--service-cluster` 命令行选项决定。如果要启用此功能，你需要设置 :ref:`user_agent <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.add_user_agent>` 选项为 true。

.. _config_http_conn_man_headers_downstream-service-node:

x-envoy-downstream-service-node
-------------------------------

内部服务通常想知道哪个下游节点在调用它们。该头部与 :ref:`config_http_conn_man_headers_downstream-service-cluster` 非常类似，不同之处在于它的值来自于 :option:`--service-node` 选项。

.. _config_http_conn_man_headers_x-envoy-external-address:

x-envoy-external-address
------------------------

服务希望根据原始客户端的 IP 地址做分析，这是一种常见的案例。然而真的实现它却可能是一件非常复杂的事情，可以参阅关于 :ref:`XFF <config_http_conn_man_headers_x-forwarded-for>` 的冗长讨论。因此 Envoy 提供的简化方案是，当请求来源于外部客户端时，将 *x-envoy-external-address* 设为 :ref:`可信客户端地址 <config_http_conn_man_headers_x-forwarded-for_trusted_client_address>`，当请求来源于内部时，将 *x-envoy-external-address* 设空或重置。为了达到分析目的，可以在内部服务之间安全地转发此头部，而无需处理复杂的 XFF。

.. _config_http_conn_man_headers_x-envoy-force-trace:

x-envoy-force-trace
-------------------

如果内部请求设置了这个头部，Envoy 会修改生成的 :ref:`config_http_conn_man_headers_x-request-id` 并强制采集跟踪信息。这也使得响应头部中强制返回  :ref:`config_http_conn_man_headers_x-request-id`。如果 request ID 随后传播到其它主机，这些主机也会采集跟踪信息，从而形成一个完整的请求跟踪链。请参考 :ref:`tracing.global_enabled <config_http_conn_man_runtime_global_enabled>` 和 :ref:`tracing.random_sampling <config_http_conn_man_runtime_random_sampling>` 的运行时配置。

.. _config_http_conn_man_headers_x-envoy-internal:

x-envoy-internal
----------------

服务通常想知道请求是否来源于内部。Envoy 使用 :ref:`XFF <config_http_conn_man_headers_x-forwarded-for>` 作为判断依据来决定是否将该值设置为 *true*。这有利于避免解析和处理 XFF。

.. _config_http_conn_man_headers_x-envoy-original-dst-host:

x-envoy-original-dst-host
-------------------------

当启用 :ref:`原始目标 <arch_overview_load_balancing_types_original_destination>` 负载均衡策略时，可以使用该头部来覆盖目标地址。

默认设置是忽略该头部，除非通过 :ref:`use_http_header <envoy_v3_api_field_config.cluster.v3.Cluster.OriginalDstLbConfig.use_http_header>` 启用。

.. _config_http_conn_man_headers_x-forwarded-client-cert:

x-forwarded-client-cert
-----------------------

*x-forwarded-client-cert* (XFCC) 是一个代理头部，它携带了从客户端到服务器的请求路径中的部分或全部客户端、代理服务器的证书信息。代理服务器可以在代理请求之前清理/追加/转发 XFCC 头部。

XFCC 头部值是一个用逗号（“,”）分隔的字符串。每个子字符串是一个 XFCC 元素，它保存了每个代理添加的信息。代理可以将当前客户端证书信息作为 XFCC 元素追加到请求的 XFCC 头部的结尾，并用逗号分隔。

每个 XFCC 元素是一个用分号“;”分隔的字符串。每个子字符串是一个用等号（“=”）组合的键值对。键不区分大小写，值区分大小写。如果值中存在字符 “,”、“;”或“=”，则应该用双引号标出。如果值中存在字符双引号，则应该使用反斜杠加双引号标出（\"）。

支持以下键名：

1. ``By`` 当前代理的证书的主体别名（Subject Alternative Name，SAN，URI 类型）。
2. ``Hash`` 当前客户端证书的 SHA 256 摘要。
3. ``Cert`` 当前客户端的完整证书，格式为 URL 编码的 PEM 格式。
4. ``Chain`` 完整的证书链（包括叶节点证书），格式为 URL 编码的 PEM 格式。
5. ``Subject`` 当前客户端的主题（Subject）字段。该值总是用双引号括起来。
6. ``URI`` 当前客户端的证书的 URI 类型的 SAN 字段。
7. ``DNS`` 当前客户端的证书的 DNS 类型的 SAN 字段。一个客户端证书可能包含多个 DNS 类型的 SAN 字段，这些字段都是独立的键值对。

一个客户端证书可能包含多个 SAN 类型。关于不同 SAN 类型的说明，请参考 `RFC 2459`_。

.. _RFC 2459: https://tools.ietf.org/html/rfc2459#section-4.2.1.7

以下为 XFCC 头部的一些例子：

1. 单客户端证书，且只有 URI 类型 SAN 字段的例子：``x-forwarded-client-cert: By=http://frontend.lyft.com;Hash=468ed33be74eee6556d90c0149c1309e9ba61d6425303443c0748a02dd8de688;Subject="/C=US/ST=CA/L=San Francisco/OU=Lyft/CN=Test Client";URI=http://testclient.lyft.com``
2. 两个客户端证书，且只有 URI 类型 SAN 字段的例子：``x-forwarded-client-cert: By=http://frontend.lyft.com;Hash=468ed33be74eee6556d90c0149c1309e9ba61d6425303443c0748a02dd8de688;URI=http://testclient.lyft.com,By=http://backend.lyft.com;Hash=9ba61d6425303443c0748a02dd8de688468ed33be74eee6556d90c0149c1309e;URI=http://frontend.lyft.com``
3. 单客户端证书，但同时有 URI 类型和 DNS 类型 SAN 字段的例子：``x-forwarded-client-cert: By=http://frontend.lyft.com;Hash=468ed33be74eee6556d90c0149c1309e9ba61d6425303443c0748a02dd8de688;Subject="/C=US/ST=CA/L=San Francisco/OU=Lyft/CN=Test Client";URI=http://testclient.lyft.com;DNS=lyft.com;DNS=www.lyft.com``

Envoy 处理 XFCC 的方式由 :ref:`forward_client_cert_details<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.forward_client_cert_details>` 和 :ref:`set_current_client_cert_details<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.set_current_client_cert_details>`
HTTP 连接管理器选项指定。如果未设置 *forward_client_cert_details*，默认情况下会清理 XFCC 头部。

.. _config_http_conn_man_headers_x-forwarded-for:

x-forwarded-for
---------------

*x-forwarded-for* (XFF) 是一个标准的代理头部，它携带了请求从客户端到服务器的路径上流经的每个节点的 IP 地址。遵守兼容规范的代理会在代理请求前，将最近一个客户端的 IP *追加* 至 XFF 列表的末端。XFF 的一些例子是：

1. ``x-forwarded-for: 50.0.0.1`` （单客户端）
2. ``x-forwarded-for: 50.0.0.1, 40.0.0.1`` （外部代理跳）
3. ``x-forwarded-for: 50.0.0.1, 10.0.0.1`` （内部代理跳）

Envoy 追加 XFF 的前提是 :ref:`use_remote_address
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.use_remote_address>`
HTTP 连接管理选项设置为 true 并且 :ref:`skip_xff_append
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.skip_xff_append>` 设置为 false。这意味着如果 *use_remote_address* 为 false（这是默认值）或 *skip_xff_append* 为 true，则连接管理器将以不修改 XFF 的透明模式运行。

.. attention::

  通常来说，如果把 Envoy 作为边缘节点（又名前端代理）部署使用时，应当将 *use_remote_address* 设置为 true。如果将 Envoy 作为网格中的内部服务节点部署时，应当将它设置为 false。

.. _config_http_conn_man_headers_x-forwarded-for_trusted_client_address:

*use_remote_address* 的值控制 Envoy 如何确定*可信客户端地址*。当 HTTP 请求经由一系列代理（零个或多个）传到 Envoy，可信的客户端地址是指已知准确的源 IP 地址中最早的那个。与 Envoy 直接连接的下游节点的源 IP 地址是可信的。XFF 在*有些场景下*是可信的。恶意客户端可以伪造 XFF，但如果 XFF 中最后一个地址是由可信代理传入的，那么它也是可信的。

Envoy 用于确定可信客户端地址（在向 XFF 追加任何内容*之前*）的默认规则是：

* 如果 *use_remote_address* 为 false 且请求的 XFF 中至少包含一个 IP address，则可信客户端地址取 XFF 中*最后*（即最右）一个 IP 地址。
* 否则，可信客户端地址取与 Envoy 直接连接的下游节点的源 IP 地址。

如果在边缘部署的 Envoy 实例前还部署有一个或多个可信的代理时，可以使用 *xff_num_trusted_hops* 配置项来信任更多的来自于 XFF 的地址。

* 如果 *use_remote_address* 为 false 且 *xff_num_trusted_hops* 被设置为一个大于零的值 *N*，则可信客户端地址为 XFF 右侧起的第 N+1 个地址。（如果 XFF 中的地址数量少于 N+1，则 Envoy 会使用直接连接的下游节点的源 IP 地址）。
* 如果 *use_remote_address* 为 true 且 *xff_num_trusted_hops* 被设置为一个大于零的值 *N*，则可信客户端地址为 XFF 右侧起的第 N 个地址。（如果 XFF 中的地址数量少于 N，则 Envoy 会使用直接连接的下游节点的源 IP 地址）。

Envoy 使用可信的客户端地址内容来确定请求是发起于外部还是内部。这会影响是否设置了 :ref:`config_http_conn_man_headers_x-envoy-internal` 头部。

示例 1：Envoy 作为边缘代理，在它前面没有可信代理
    设置：
      | use_remote_address = true
      | xff_num_trusted_hops = 0

    请求详情：
      | Downstream IP address = 192.0.2.5
      | XFF = "203.0.113.128, 203.0.113.10, 203.0.113.1"

    结果：
      | Trusted client address = 192.0.2.5 （忽略了 XFF）
      | X-Envoy-External-Address 被设置为 192.0.2.5
      | XFF 被改成了 "203.0.113.128, 203.0.113.10, 203.0.113.1, 192.0.2.5"
      | X-Envoy-Internal 被删除（如果在请求中带了这个头部）

示例 2：Envoy 作为内部代理，在它前面有一个如示例 1 的边缘代理
    设置：
      | use_remote_address = false
      | xff_num_trusted_hops = 0

    请求详情：
      | Downstream IP address = 10.11.12.13 （即边缘 Envoy 代理的地址）
      | XFF = "203.0.113.128, 203.0.113.10, 203.0.113.1, 192.0.2.5"

    结果：
      | Trusted client address = 192.0.2.5 （XFF 中的最后一个地址为可信地址）
      | X-Envoy-External-Address 没有改变
      | X-Envoy-Internal 被删除（如果在请求中带了这个头部）

示例 3：Envoy 作为边缘代理，在它前面有两个信任的外部代理
    设置：
      | use_remote_address = true
      | xff_num_trusted_hops = 2

    请求详情：
      | Downstream IP address = 192.0.2.5
      | XFF = "203.0.113.128, 203.0.113.10, 203.0.113.1"

    结果：
      | Trusted client address = 203.0.113.10 （XFF 中的倒数第 2 个地址为可信地址）
      | X-Envoy-External-Address 被设置为 203.0.113.10
      | XFF 被改成了 "203.0.113.128, 203.0.113.10, 203.0.113.1, 192.0.2.5"
      | X-Envoy-Internal 被删除（如果在请求中带了这个头部）

示例 4：Envoy 作为内部代理, 它前面有一个如示例 3 的边缘代理
    设置：
      | use_remote_address = false
      | xff_num_trusted_hops = 2

    请求详情：
      | Downstream IP address = 10.11.12.13 （边缘 Envoy 代理的地址）
      | XFF = "203.0.113.128, 203.0.113.10, 203.0.113.1, 192.0.2.5"

    结果：
      | Trusted client address = 203.0.113.10
      | X-Envoy-External-Address 没有改变
      | X-Envoy-Internal 被删除（如果在请求中带了这个头部）

示例 5：Envoy 作为内部代理，接收来自一个内部客户端的请求
    设置：
      | use_remote_address = false
      | xff_num_trusted_hops = 0

    请求详情：
      | Downstream IP address = 10.20.30.40 （内部客户端的地址）
      | XFF 不存在

    结果：
      | Trusted client address = 10.20.30.40
      | X-Envoy-External-Address 保持未设置
      | X-Envoy-Internal 被设置为 "false"

示例 6：来自示例 5 的内部 Envoy，接收由另外一个 Envoy 代理的请求
    设置：
      | use_remote_address = false
      | xff_num_trusted_hops = 0

    请求详情：
      | Downstream IP address = 10.20.30.50 （将请求代理至本机的另一台 Envoy 实例的地址）
      | XFF = "10.20.30.40"

    结果：
      | Trusted client address = 10.20.30.40
      | X-Envoy-External-Address 保持未设置
      | X-Envoy-Internal 被设置为 "true"

关于 XFF 的一些非常重要的点：

1. 如果 *use_remote_address* 被设置为 true，Envoy 会将 :ref:`config_http_conn_man_headers_x-envoy-external-address` 头部设置为可信的客户端地址。

.. _config_http_conn_man_headers_x-forwarded-for_internal_origin:

2. Envoy 用 XFF 来确定请求是内部源还是外部源。如果 *use_remote_address* 被设置为 true，当且仅当请求不包含 XFF 且与 Envoy 直接连接的下游节点具有内部（RFC1918 或 RFC4193）源地址时，该请求为内部请求。如果 *use_remote_address* 被设置为 false，当且仅当 XFF 包含单个 RFC1918 或 RFC4193 地址时，该请求为内部请求。

   * **注意**: 如果一个内部服务在代理外部请求至另一个内部服务时包含了原始的 XFF 头部，并且设置了 :ref:`use_remote_address <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.use_remote_address>`，那么 Envoy 将在出口处继续追加。这会导致对方认为请求是来自外部的。通常来说，这就是传递 XFF 头部的目的。但如果场景并非如此，不要传递 XFF，应该改用 :ref:`config_http_conn_man_headers_x-envoy-internal`。

   * **注意**: 如果将内部服务调用转发到其它内部服务（保留 XFF），Envoy 将不会认为这是一个内部服务。这是一个已知的“bug”，原因是 XFF 将解析以及判定一个请求是否来自内部的工作进行了简化。在此种场景下，请不要转发 XFF，应该让 Envoy使用一个内部原始 IP 生成一个新的 XFF。

.. _config_http_conn_man_headers_x-forwarded-proto:

x-forwarded-proto
-----------------

通常，服务想要知道前端/边缘 Enovy 始发处的协议是什么（HTTP 或 HTTPS）。*x-forwarded-proto* 包含了这些信息。它会被设置为 *http* 或 *https*。

.. _config_http_conn_man_headers_x-request-id:

x-request-id
------------

Envoy 使用 *x-request-id* 头部来唯一标识请求并执行稳定的访问日志记录和跟踪。Envoy 将为所有外部来源请求生成一个 *x-request-id* 头部（原头部被清理）。它还会为没有 *x-request-id* 头部的内部请求新生成一个。这意味着 *x-request-id* 可以并且应该在客户端应用程序间传播，以便在整个网格中拥有一个稳定的 ID。由于 Envoy 的进程外架构，Envoy 本身不能自动地转发头部。这是少数领域应当引入瘦客户端库来完成工作的一个例子。具体如何去做，这个话题超出了本文档的范围。如果能做到 *x-request-id* 跨所有主机传播，则可以使用如下功能：

* 通过 :ref:`v3 API runtime filter<envoy_v3_api_field_config.accesslog.v3.AccessLogFilter.runtime_filter>` 实现稳定的 :ref:`access logging <config_access_log>`。
* 通过开启 :ref:`tracing.random_sampling
  <config_http_conn_man_runtime_random_sampling>` 运行时配置，或通过强制开启基于 :ref:`config_http_conn_man_headers_x-envoy-force-trace` 和 :ref:`config_http_conn_man_headers_x-client-trace-id` 头部的追踪，实现稳定的追踪或随机抽样追踪。

.. _config_http_conn_man_headers_x-ot-span-context:

x-ot-span-context
-----------------

当采用 LightStep 追踪方案时，Envoy 使用 *x-ot-span-context* HTTP 头部在追踪区间之间建立适当的父子关系。例如，出口区间是入口区间的子节点（如果入口区间存在）。Envoy 在入口请求注入 *x-ot-span-context* 并将其转发给本地服务。Envoy 依赖应用程序将出口处的 *x-ot-span-context* 传播给上游。更多资料请参考 :ref:`here <arch_overview_tracing>`。

.. _config_http_conn_man_headers_x-b3-traceid:

x-b3-traceid
------------

当采用 Zipkin 追踪方案时，Envoy 会用到 *x-b3-traceid* HTTP 头部。TraceId 的长度为 64-bit，它标识了总体的追踪 ID。追踪中的每个区间都共享此 ID。更多资料请参考 `<https://github.com/openzipkin/b3-propagation>`。

.. _config_http_conn_man_headers_x-b3-spanid:

x-b3-spanid
-----------

当采用 Zipkin 追踪方案时，Envoy 会用到 *x-b3-spanid* HTTP 头部。SpanId 的长度为 64-bit，它标识了当前操作在追踪树中的位置。该值不应该被转译：它可能是从 TraceId 派生出来的，也可能不是。关于 Zipkin 的更多资料请参考 `<https://github.com/openzipkin/b3-propagation>`。

.. _config_http_conn_man_headers_x-b3-parentspanid:

x-b3-parentspanid
-----------------

当采用 Zipkin 追踪方案时，Envoy 会用到 *x-b3-parentspanid* HTTP 头部。ParentSpanId 的长度为 64-bit，它标识了父操作在追踪树中的位置。如果该区间是追踪树的根节点，那么就没有 ParentSpanId。关于 Zipkin 的更多资料请参考 `<https://github.com/openzipkin/b3-propagation>`。

.. _config_http_conn_man_headers_x-b3-sampled:

x-b3-sampled
------------

当采用 Zipkin 追踪方案时，Envoy 会用到 *x-b3-sampled* HTTP 头部。如果没有设置 Sampled 标记或被设置为 1，该区间会被上报到追踪系统。一旦 Sampled 标记被设置了 0 或 1 的值，那么这个值应当被传递至下游且保持不变。关于 Zipkin 的更多资料请参考 `<https://github.com/openzipkin/b3-propagation>`。

.. _config_http_conn_man_headers_x-b3-flags:

x-b3-flags
----------

当采用 Zipkin 追踪方案时，Envoy 会用到 *x-b3-flags* HTTP 头部。它被用来编码单个或多个选项。例如 Debug 被编码为 ``X-B3-Flags: 1``。关于 Zipkin 的更多资料请参考 `<https://github.com/openzipkin/b3-propagation>`。

.. _config_http_conn_man_headers_b3:

b3
----------

当采用 Zipkin 追踪方案时，Envoy 会用到 *b3* HTTP 头部。这是一个压缩过的头部格式。关于 Zipkin 的更多资料请参考 `<https://github.com/openzipkin/b3-propagation#single-header>`。

.. _config_http_conn_man_headers_x-datadog-trace-id:

x-datadog-trace-id
------------------

当使用 Datadog 追踪方案时，Envoy 会用到 *x-datadog-trace-id* HTTP 头部。该值的长度为 64-bit，它标识了整个追踪过程，和用来关联各个区间。

.. _config_http_conn_man_headers_x-datadog-parent-id:

x-datadog-parent-id
-------------------

当使用 Datadog 追踪方案时，Envoy 会用到 *x-datadog-parent-id* HTTP 头部。该值的长度为 64-bit，它唯一标识了追踪的每个区间，和用来标识区间之间的父子关系。

.. _config_http_conn_man_headers_x-datadog-sampling-priority:

x-datadog-sampling-priority
---------------------------

当使用 Datadog 追踪方案时，Envoy 会用到 *x-datadog-sampling-priority* HTTP 头部。该值为 integer 类型，用来标识当前追踪的取样策略。值为 0 即追踪不需要被上报，值为 1 即应该被取样和上报。

.. _config_http_conn_man_headers_custom_request_headers:

自定义的请求/响应头部
-------------------------------------

可以在加权集群、路由、虚拟主机和/或全局路由配置级别将自定义请求/响应头部添加到请求/响应中。参考 :ref:`v3 <envoy_v3_api_msg_config.route.v3.RouteConfiguration>` API 文档。

该机制不会改动 *:-prefixed* pseudo-header。该机制可能会改动诸如 :ref:`prefix_rewrite <envoy_v3_api_field_config.route.v3.RouteAction.prefix_rewrite>`，
:ref:`regex_rewrite <envoy_v3_api_field_config.route.v3.RouteAction.regex_rewrite>` 和 :ref:`host_rewrite <envoy_v3_api_field_config.route.v3.RouteAction.host_rewrite_literal>`。

头部将按照以下顺序追加到请求/响应中：加权集群级别头部、路由级别头部、虚拟主机级别头部以及全局级别头部。

Envoy 支持向请求和响应头部里添加动态值。用百分号（%）来分割变量名称。

.. attention::

  如果需要在请求/响应头部中添加百分号字面量，则需要重复它以达到转义的效果。例如，要发送值为 ``100%`` 的头部，那么 Envoy 配置中的自定义的头部值必须为 ``100%%``。

支持的变量名有：

%DOWNSTREAM_REMOTE_ADDRESS%
    下游连接的远端地址。如果是 IP 地址，则地址还会包含端口。

    .. attention::

      如果是从 :ref:`proxy proto <envoy_v3_api_field_config.listener.v3.FilterChain.use_proxy_proto>` 或 :ref:`x-forwarded-for
      <config_http_conn_man_headers_x-forwarded-for>` 推断出的地址，那么它可能并不是远端真实的物理地址。

%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%
    同上 **%DOWNSTREAM_REMOTE_ADDRESS%**，区别在于即使地址是 IP 地址，也不会包含端口。

%DOWNSTREAM_LOCAL_ADDRESS%
    下游连接的本地地址，如果是 IP 地址，则地址还会包含端口。如果原始连接被 iptables REDIRECT 重定向，则该值标识 :ref:`Original Destination Filter <config_listener_filters_original_dst>` 使用 SO_ORIGINAL_DST Socket 选项恢复的原始目标地址。如果原始连接被 iptables TPROXY 重定向，且监听器的透明选项设置为 true，则该值标识原始目标地址和端口。

%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%
    同上 **%DOWNSTREAM_LOCAL_ADDRESS%**，区别在于即使地址是 IP 地址，也不会包含端口。

%DOWNSTREAM_LOCAL_PORT%
    和 **%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%** 类似，但仅包含 **%DOWNSTREAM_LOCAL_ADDRESS%** 的端口部分。

%DOWNSTREAM_LOCAL_URI_SAN%
  HTTP
    用以和下游建立 TSL 连接时使用的本地证书中的 SAN 字段的 URI。
  TCP
    用以和下游建立 TSL 连接时使用的本地证书中的 SAN 字段的 URI。

%DOWNSTREAM_PEER_URI_SAN%
  HTTP
    用以和下游建立 TSL 连接时使用的对等证书中的 SAN 字段的 URI。
  TCP
    用以和下游建立 TSL 连接时使用的对等证书中的 SAN 字段的 URI。

%DOWNSTREAM_LOCAL_SUBJECT%
  HTTP
    用以和下游建立 TSL 连接时使用的本地证书中的 subject。
  TCP
    用以和下游建立 TSL 连接时使用的本地证书中的 subject。

%DOWNSTREAM_PEER_SUBJECT%
  HTTP
    用以和下游建立 TSL 连接时使用的对等证书中的 subject。
  TCP
    用以和下游建立 TSL 连接时使用的对等证书中的 subject。

%DOWNSTREAM_PEER_ISSUER%
  HTTP
    用以和下游建立 TSL 连接时使用的对等证书中的 issuer。
  TCP
    用以和下游建立 TSL 连接时使用的对等证书中的 issuer。

%DOWNSTREAM_TLS_SESSION_ID%
  HTTP
    用以和下游建立 TSL 连接时使用的对等证书中的 session ID。
  TCP
    用以和下游建立 TSL 连接时使用的对等证书中的 session ID。

%DOWNSTREAM_TLS_CIPHER%
  HTTP
    用以和下游建立 TSL 连接时使用的加密组的 OpenSSL 名称。
  TCP
    用以和下游建立 TSL 连接时使用的加密组的 OpenSSL 名称。

%DOWNSTREAM_TLS_VERSION%
  HTTP
    用以和下游建立 TSL 连接时使用的 TLS 版本（如 ``TLSv1.2``, ``TLSv1.3``）。
  TCP
    用以和下游建立 TSL 连接时使用的 TLS 版本（如 ``TLSv1.2``, ``TLSv1.3``）。

%DOWNSTREAM_PEER_FINGERPRINT_256%
  HTTP
    用以和下游建立 TSL 连接时使用的客户端证书的 16 进制编码的 SHA256 指纹。
  TCP
    用以和下游建立 TSL 连接时使用的客户端证书的 16 进制编码的 SHA256 指纹。

%DOWNSTREAM_PEER_FINGERPRINT_1%
  HTTP
    用以和下游建立 TSL 连接时使用的客户端证书的 16 进制编码的 SHA1 指纹。
  TCP
    用以和下游建立 TSL 连接时使用的客户端证书的 16 进制编码的 SHA1 指纹。

%DOWNSTREAM_PEER_SERIAL%
  HTTP
    用以和下游建立 TSL 连接时使用的客户端证书的 16 进制编码的序列号。
  TCP
    用以和下游建立 TSL 连接时使用的客户端证书的 16 进制编码的序列号。

%DOWNSTREAM_PEER_CERT%
  HTTP
    用以和下游建立 TSL 连接时使用的客户端证书的 URL 编码 PEM 格式。
  TCP
    用以和下游建立 TSL 连接时使用的客户端证书的 URL 编码 PEM 格式。

%DOWNSTREAM_PEER_CERT_V_START%
  HTTP
    用以和下游建立 TSL 连接时使用的客户端证书的起始有效期。
  TCP
    用以和下游建立 TSL 连接时使用的客户端证书的起始有效期。

%DOWNSTREAM_PEER_CERT_V_END%
  HTTP
    用以和下游建立 TSL 连接时使用的客户端证书的结束有效期。
  TCP
    用以和下游建立 TSL 连接时使用的客户端证书的结束有效期。

%HOSTNAME%
    系统主机名。

%PROTOCOL%
    原始协议名，已由 Envoy 通过 :ref:`x-forwarded-proto <config_http_conn_man_headers_x-forwarded-proto>` 请求头部添加。

%UPSTREAM_METADATA(["namespace", "key", ...])%
    用来自路由选中的上游主机的 :ref:`EDS endpoint metadata <envoy_v3_api_field_config.endpoint.v3.LbEndpoint.metadata>` 填充头部。元数据可以从任何命名空间中选择。元数据值可以是字符串、数字、布尔值、列表、嵌套结构或空值。如果指定了多个键，可以从嵌套结构中选择上游元数据值，否则只支持字符串、布尔值或数字。如果未找到命名空间或键，则不会发送头部。命名空间和键通过 JSON 字符串数组指定。最后，如果在选中值中存在百分号，或者选中值不是支持的类型，则不会发送头部。**不要** 通过重复百分号来转义。上游的元数据是无法被添加到请求头部中的，因为生成请求头部时上游主机还没有被选中。

%DYNAMIC_METADATA(["namespace", "key", ...])%
    与 UPSTREAM_METADATA 类似，可以用请求中的动态元数据填充头部。（例如来自于过滤器，如 header-to-metadata 过滤器）。

    该功能在请求和响应头部中都适用。

%UPSTREAM_REMOTE_ADDRESS%
    上游主机的远端地址。如果该地址是一个 IP，那么它会包含地址和端口。上游的远端地址是无法被添加到请求头部中的，因为生成自定义请求头部时上游主机还没有被选中。

%PER_REQUEST_STATE(reverse.dns.data.name)%
    将流信息中通过 filterState() 对象设置的值填充入头部。为了能在自定义的请求/响应头部中使用，这些值的类型必须是 Envoy::Router::StringAccessor。这些值的命名必须符合标准的反向 DNS 规范，用以标识创建该值的组织的唯一名称。

%REQ(header-name)%
    用请求头部中的指定值来填充。

%START_TIME%
    请求开始时间。START_TIME 可以通过配置 :ref:`access log format rules<config_access_log_format_start_time>` 来格式化。

    在自定义头部里添加毫秒精度的时间的例子：

    .. code-block:: none

      route:
        cluster: www
      request_headers_to_add:
        - header:
            key: "x-request-start"
            value: "%START_TIME(%s.%3f)%"
          append: true

%RESPONSE_FLAGS%
    有关响应或连接的附加信息。可选的值和含义请参考 access log formatter :ref:`documentation<config_access_log_format_response_flags>`。

%RESPONSE_CODE_DETAILS%
    响应码详情。可以提供关于 HTTP 响应码的附加信息，例如谁/为什么（上游或 Envoy）设置了这个值。
