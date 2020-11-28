.. _arch_overview_upgrades:

HTTP 升级
==========

Envoy 升级主要用于支持 WebSocket 和 CONNECT 在 HTTP 请求中的的升级，同时也支持其他的升级。
升级操作通过 HTTP 过滤链来传递 HTTP header 和升级的负载信息。
除此之外也可以使用或者不使用自定义过滤链来配置 :ref:`upgrade_configs <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.upgrade_configs>`
如果指定了 :ref:`upgrade_type <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.UpgradeConfig.upgrade_type>`
的值，那么升级的 HTTP header 信息、请求体、返回体、HTTP 负载数据默认都将通过 HTTP 过滤器链。
此外为了避免升级负载仅使用 HTTP 过滤器，可以为给定的升级类型设置自定义的过滤器，同时也只能使用路由
:ref:`filters <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.UpgradeConfig.filters>` 将数据发往上游。

升级可以根据 :ref:`每个路由 <envoy_v3_api_field_config.route.v3.RouteAction.upgrade_configs>` 配置来启用还是禁用。
同时任意的路由启用/禁用都会自动覆盖 HttpConnectionManager 配置信息。
如下图所示，自定义过滤器链只能基于每个 HttpConnectionManager 进行配置。

+-----------------------+-------------------------+-------------------+
| *启用 HCM 升级*       | *启用路由升级*          | *启用升级*        |
+=======================+=========================+===================+
| T (Default)           | T (Default)             | T                 |
+-----------------------+-------------------------+-------------------+
| T (Default)           | F                       | F                 |
+-----------------------+-------------------------+-------------------+
| F                     | T (Default)             | T                 |
+-----------------------+-------------------------+-------------------+
| F                     | F                       | F                 |
+-----------------------+-------------------------+-------------------+

注意！所有升级的统计信息是绑定在一起的，例如 WebSocket 或者其它的 HTTP 升级都是通过 ``downstream_cx_upgrades_total`` 和 ``downstream_cx_upgrades_active`` 来统计信息的。

HTTP/2 上的 WebSocket
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

默认情况下，HTTP/2 对 WebSocket 的支持都是关闭的，但 Envoy 却支持 WebSocket 在 HTTP/2 流上进行隧道传输，以便在整个部署过程中可以使用统一的 HTTP/2 网络。
例如，可以这样进行部署：

[Client] ---- HTTP/1.1 ---- [Front Envoy] ---- HTTP/2 ---- [Sidecar Envoy ---- H1  ---- App]

在上面的示例中，客户端需要使用 WebSocket，同时我们也希望 WebSocket 可以直接到达上游业务服务器，那就意味着只穿过 HTTP/2 即可到达。

上述实例中的方法是通过扩展连接（ `RFC8441 <https://tools.ietf.org/html/rfc8441>`_ ）支持实现的，通过在第二层 Envoy 中设置 :ref:`allow_connect <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.allow_connect>` 打开的。
在 WebSocket 请求被转换为 HTTP CONNECT 流时，其中包含protocol 头指示原始升级、遍历跳转 HTTP/2 、降级回 HTTP/1.1 WebSocket Upgrade。
相同的 Upgrade-CONNECT-Upgrade 转换将会在任意 HTTP/2 上跳转执行，即假设 HTTP/1.1 中请求方法为 GET 。
非 WebSocket 升级则允许任何有效的 HTTP 方法（例如 POST 请求），当前的升级/降级机制也会丢弃原有的请求方式，最终Envoy 会将请求转换为去往上游的 GET 方法。

注意！HTTP/2 升级会非常严格的要求 HTTP/1.1 的路径，因此不能使用代理用于 WebSocket 升级的请求和响应。

CONNECT 支持
^^^^^^^^^^^^^^

默认情况下，Envoy 内部的 CONNECT 支持都是处于关闭状态的（Envoy 会返回403 状态码以响应 CONNECT 请求）。
因此，可以通过上述的选项来启用 CONNECT 支持，其中将设置特殊关键字为 “CONNECT”。

在 HTTP/2 中，CONNECT 请求可能会是一个路径，但是在 HTTP/1.1 中，CONNECT 请求通常是没有路径的，只能使用 :ref:`connect_matcher <envoy_v3_api_msg_config.route.v3.RouteMatch.ConnectMatcher>` 进行匹配。

注意！当对 CONNECT 请求执行非通配符域匹配时，CONNECT 的目标是匹配主机和端口才能成功，而不是匹配 Host 或者授权信息。

Envoy 可以使用两种方法来处理 CONNECT 请求，一种是代理 CONNECT 报文头，让上游终止 CONNECT 请求，就类似于其他的请求一样；或者终止 CONNECT 请求，将负载信息作为原始 TCP 信息转发。

而当 CONNECT 升级配置信息被配置时，默认情况下就是使用代理连接请求，并使用升级路径将其处理为任何其他请求。

如果需要终止，也可以通过设置 :ref:`connect_config <envoy_v3_api_field_config.route.v3.RouteAction.UpgradeConfig.connect_config>` 来完成。
如果仅仅只是针对 CONNECT 请求，路由过滤器将会去掉请求头，并将请求发往上游。
在收到来自上游的 TCP 数据时，路由就会生成 HTTP 200 响应头信息，然后将上游返回的 TCP 数据作为 HTTP 响应体。

.. note::

  如果配置不正确，这种 CONNECT 支持连接会造成严重的安全漏洞。例如如果上游的安全漏洞存在于负载中，那么上游就会转发未经初始化的头信息。请谨慎使用！

HTTP/2 上的TCP 隧道
^^^^^^^^^^^^^^^^^^^^^^^^

Envoy 还支持将原始 TCP 请求转化为 HTTP/2 CONNECT 请求，这是通过提前预备的安全链路来代理多路传输的 TCP 请求，并且可以分摊 TLS 握手的成本。
例如设置代理 SMTP 的流程如下：

[SMTP Upstream] --- raw SMTP --- [L2 Envoy]  --- SMTP tunneled over HTTP/2  --- [L1 Envoy]  --- raw SMTP  --- [Client]

如果运行 ``bazel-bin/source/exe/envoy-static –config-path configs/encapsulate_in_connect.yaml –base-id 1 and bazel-bin/source/exe/envoy-static –config-path configs/terminate_connect.yaml`` 则能在示例配置 :repo:`文件 <configs/>` 中找到对应的示例。
其中将要运行两个 Envoy ，第一个会监听 10000 端口 上的 TCP 流量，接着会将其封装为 HTTP/2 请求；另一个会监听 10001 端口上的 HTTP/2 请求，去掉请求的请求头，接着将原始 TCP 请求转到上游，在本示例中是 google.com。 