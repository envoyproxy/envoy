.. _life_of_a_request:

请求的生命周期
=================

下面，我们描述通过 Envoy 代理传递的请求的生命周期中的事件。首先，我们描述 Envoy 如何适应请求的请求路径，
然后描述在请求从下游到达 Envoy 代理之后发生的内部事件。 我们追踪该请求，直到相应的上游调度和响应路径为止。

术语
-----------

Envoy 在其代码库和文档中使用以下术语：

* *集群（Cluster）*: Envoy 将请求转发到的一组端点的逻辑服务。
* *下游（Downstream）*: 连接到 Envoy 的实体。 可能是本地应用程序（使用 Sidecar 模型）或网络节点。 在非Sidecar模型中，是一个远程客户端。
* *端点（Endpoints）*: 实现逻辑服务的网络节点。它们被按照 Cluster 分组。 Cluster 中的端点在 Envoy 代理的上游。
* *过滤器（Filter）*: 在连接或请求处理管道中提供某些方面请求处理的模块。就好比 Unix 是小型实用程序（过滤器）与 Unix 管道（过滤器链）的组合。
* *过滤器链（Filter chain）*: 一系列的过滤器。
* *监听器（Listeners）*: Envoy 模块负责绑定到 IP/port ，接受新的 TCP 连接（或 UDP 数据包），并编排面向请求处理的下游。
* *上游（Upstream）*: 转发服务请求时，Envoy 连接到的端点（网络节点）。可能是本地应用程序（使用 Sidecar 模型）或网络节点。在非 Sidecar 模型中，对应于远程后端。

网络拓扑结构
----------------

请求如何流经网络中的各个组件（包括 Envoy ）取决于网络的拓扑结构。 Envoy 可用于多种网络拓扑中。 
我们在下面重点介绍 Envoy 的内部操作，但在本节中我们将简要地介绍下 Envoy 与网络其余部分的关系。

Envoy 最初是作为 `服务网格 <https://blog.envoyproxy.io/service-mesh-data-plane-vs-control-plane-2774e720f7fc>`_ sidecar 代理，
从应用中分离了负载平衡，路由，可观察性，安全性和服务发现等功能。在服务网格模型中，请求流经 Envoy 作为网络的网关。
请求通过入口或出口监听器到达 Envoy ：

* 入口监听器从服务网格中的其他节点获取请求，并将其转发到本地应用程序。 本地应用程序的响应通过 Envoy 流回到下游。
* 出口监听器从本地应用程序获取请求，并将其转发到网络中的其他节点。 这些接收节点通常还将运行 Envoy 并通过其入口监听器接受请求。

.. image:: /_static/lor-topology-service-mesh.svg
   :width: 80%
   :align: center

.. image:: /_static/lor-topology-service-mesh-node.svg
   :width: 40%
   :align: center


Envoy 可用于服务网格之外的各种配置。 例如，它还可以充当内部负载均衡器：

.. image:: /_static/lor-topology-ilb.svg
   :width: 65%
   :align: center

或作为网络边缘上的入口/出口代理：

.. image:: /_static/lor-topology-edge.svg
   :width: 90%
   :align: center

在实践中，通常混合使用这些方法，在服务网格中，Envoy 同时作为内部负载均衡器以及在边缘上作为代理。 请求路径可能会遍历多个 Envoy 。

.. image:: /_static/lor-topology-hybrid.svg
   :width: 90%
   :align: center

Envoy 可以在多层拓扑中进行配置，以实现可伸缩性和可靠性，其中请求首先通过边缘 Envoy，然后再通过第二层 Envoy ：

.. image:: /_static/lor-topology-tiered.svg
   :width: 80%
   :align: center

在上述所有情况下，请求将从下游通过 TCP，UDP 或 Unix 域套接字到达特定的 Envoy 。 Envoy 将通过 TCP，UDP 或 Unix 
域套接字向上游转发请求。 我们在下面仅关注一个 Envoy 代理。

配置
-------------

Envoy是一个易于扩展的平台。 这导致可能的请求路径组合爆炸，具体取决于：

* L3/4 协议，例如 TCP，UDP，Unix 域套接字。
* L7 协议，例如 HTTP/1，HTTP/2，HTTP/3，gRPC，Thrift，Dubbo，Kafka，Redis 和各种数据库。
* socket 套接字，例如 纯文本，TLS，ALTS。
* 连接路由，例如 PROXY 协议，原始目的地，动态转发。
* 认证和授权。
* 熔断机制和异常值检测配置以及激活状态。
* 网络，HTTP，监听器，访问日志，运行状况检查，跟踪和统计信息扩展的许多其他配置。

一次专注于一个示例是很有帮助的，因此此示例涵盖以下内容：

* 通过 TCP 连接向下游和上游的 :ref:`TLS <arch_overview_ssl>` 发出的 HTTP/2 请求。
* :ref:`HTTP连接管理器 <arch_overview_http_conn_man>` 是唯一的 :ref:`网络过滤器 <arch_overview_network_filters>` 。
* 假设的自定义过滤器和 :ref:`路由器 <arch_overview_http_routing>` 过滤器作为 :ref:`HTTP过滤器 <arch_overview_http_filters>` 链。
* :ref:`文件系统访问日志记录 <arch_overview_access_logs_sinks>` 。
* :ref:`统计下沉 <envoy_v3_api_msg_config.metrics.v3.StatsSink>` 。
* 具有静态端点的单个 :ref:`集群 <arch_overview_cluster_manager>` 。

为了简单起见，我们假定使用静态引导程序配置文件：

.. literalinclude:: _include/life-of-a-request.yaml
    :language: yaml

高层架构
-----------------------

Envoy 中的请求处理路径包括两个主要部分：

* :ref:`监听器子系统 <arch_overview_listeners>` 对**下游**请求进行处理。 它还负责管理下游请求生命周期
以及到客户端的响应路径。 下游 HTTP/2 编解码器位于此处。
* :ref:`集群子系统 <arch_overview_cluster_manager>` 负责选择和配置到端点的**上游**连接。这是了解集群
和端点运行状况并具有负载平衡和连接池功能。 上游 HTTP/2 编解码器位于此处。

这两个子系统与 HTTP 路由过滤器桥接，该过滤器将 HTTP 请求从下游转发到上游。

.. image:: /_static/lor-architecture.svg
   :width: 80%
   :align: center

我们使用上面的术语 :ref:`监听器子系统 <arch_overview_listeners>` 和 :ref:`集群子系统 
<arch_overview_cluster_manager>` 来指代由顶级 `ListenerManager` 和 `ClusterManager` 
类创建的模块和实例类的组。这些管理系统在请求之前和请求的过程中会实例化许多我们在下面讨论的组件，
例如监听器，过滤器链，编解码器，连接池和负载均衡等数据结构。

Envoy 具有 `基于事件的线程模型 <https://blog.envoyproxy.io/envoy-threading-model-a8d44b922310>`_ 。
主线程负责服务器的生命周期，配置处理，信息统计等。:ref:`工作线程 <arch_overview_threading>` 负责请求处理。
所有线程都围绕事件循环（`libevent <https://libevent.org/>`_）运行，并且任何给定的下游 TCP 连接
（包括其上的所有多路复用流）都将由一个工作线程在其生命周期内完全处理。 每个工作线程都维护自己的与上游端点的 TCP 连接池。
利用 SO_REUSEPORT 使内核始终将源/目标 IP:port 元组散列到同一工作线程进行 :ref:`UDP <arch_overview_listeners_udp>` 处理。
UDP过滤器状态被给定的工作线程共享，使用该过滤器可以根据需要提供会话语义。 这与我们下面讨论的面向连接的 TCP 过滤器形成对比，
在 TCP 过滤器中，每个连接均存在过滤器状态，而对于 HTTP 过滤器，则是基于请求进行过滤。

请求流程
------------

总览
^^^^^^^^

使用上面的示例配置简要概述请求和响应的生命周期：

1. 在 :ref:`工作线程 <arch_overview_threading>` 上运行的 Envoy :ref:`监听器 <arch_overview_listeners>` 接受来自下游的 TCP 连接。
2. :ref:`监听过滤器 <arch_overview_listener_filters>` 链已创建并运行。 它可以提供 SNI 和 pre-TLS 信息。完成后，
   监听器将匹配网络过滤器链。每个监听器可能具有多个过滤器链，这些过滤器链是在目标 IP CIDR 范围，SNI，ALPN，源端口等的某种组合上匹配。
   传输套接字（在我们的情况下为TLS传输套接字）与此过滤器链相关联。
3. 在进行网络读取时， :ref:`TLS <arch_overview_ssl>` 传输套接字将从 TCP 连接读取的数据解密为解密的数据流，以进行进一步处理。
4. :ref:`网络过滤器 <arch_overview_network_filters>` 链已创建并运行。HTTP 最重要的过滤器是 HTTP 连接管理器，它是链中的最后一个网络过滤器。
5.  :ref:`HTTP 连接管理器 <arch_overview_http_conn_man>` 中的 HTTP/2 编解码器将解密后的数据流从 TLS 连接解帧并解复用为多个独立的流。 
   每个流只处理一个请求和响应。
6. 对于每个 HTTP 请求流，都会创建并运行 :ref:`HTTP 过滤器 <arch_overview_http_filters>` 链。该请求首先通过可以读取和修改请求的自定义过滤器。
   路由过滤器是最重要的 HTTP 过滤器，它位于 HTTP 过滤器链的末尾。在路由过滤器上调用 `decodeHeaders` 时，将选择路由和集群。数据流上的请求
   头被转发到该集群中的上游端点。 :ref:`路由 <arch_overview_http_routing>` 过滤器通过从集群管理器中匹配到的集群获取HTTP连接池，以执行操作。
7. 执行集群特定的 :ref:`负载平衡 <arch_overview_load_balancing>` 以查找端点。通过检查集群的断路器，以确定是否允许新的数据流。如果端点的连接池
   为空或容量不足，则会创建到端点的新连接。
8. 上游端点连接的 HTTP/2 编解码器将请求流与通过单个 TCP 连接流向上游的任何其他流进行多路复用和帧化。
9. 上游端点连接的 TLS 传输套接字对这些字节进行加密，并将其写入上游连接的 TCP 套接字。
10. 由请求头，可选的请求体和尾部组成的请求在上游被代理，而响应在下游被代理。响应以与请求 :ref:`相反的顺序 <arch_overview_http_filters_ordering>` 
   通过 HTTP 过滤器，从路由器过滤器开始并通过自定义过滤器，然后再发送到下游。
11. 当响应完成后，请求流将被销毁。 请求后处理程序将更新统计信息，写入访问日志并最终确定跟踪范围。

我们将在以下各节中详细介绍每个步骤。

1. 监听 TCP 流量进入
^^^^^^^^^^^^^^^^^^^^^^

.. image:: /_static/lor-listeners.svg
   :width: 90%
   :align: center

*ListenerManager* 负责获取代表 :ref:`监听器 <arch_overview_listeners>` 的配置，并实例化绑定到其各自 IP/ports 的多个监听器实例。监听器可能处于以下三种状态之一：

* *Warming*: 监听器正在等待配置依赖项（例如，路由配置，动态密钥）。监听器尚未准备好接受 TCP 连接。
* *Active*: 监听器绑定到其 IP/port 并接受 TCP 连接。
* *Draining*: 监听器不再接受新的 TCP 连接，只允许现有的 TCP 连接继续运行直至结束。

每个 :ref:`工作线程<arch_overview_threading>` 为每个已配置的监听器维护自己的*监听器*实例。每个监听器都可以通过 SO_REUSEPORT 绑定到同一端口，
或者共享一个绑定到该端口的套接字。当新的 TCP 连接到达时，内核决定哪个工作线程将接受该连接，并且该工作线程的监听器将对 
``Server::ConnectionHandlerImpl::ActiveTcpListener::onAccept()`` 进行回调。

2. 监听过滤器链和网络过滤器链匹配
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

工作线程的侦听器将创建并运行 :ref:`监听过滤器 <arch_overview_listener_filters>` 链。过滤器链是通过应用每个过滤器的*过滤器工厂*而创建的。 
过滤器工厂知道过滤器的配置，并为每个连接或流创建一个新的过滤器实例。

对于我们的 TLS 监听器配置，监听过滤器链由 :ref:`TLS 检查器 <config_listener_filters_tls_inspector>` （``envoy.filters.listener.tls_inspector``）组成。
该过滤器检查初始 TLS 握手并提取服务器名称（SNI）。然后使用 SNI 进行过滤器链匹配。同时，TLS 检查器明确显示在监听过滤器链配置中，Envoy 还可以自动插入每当监听器的过滤器链中需要 SNI（或 ALPN ）。

.. image:: /_static/lor-listener-filters.svg
   :width: 80%
   :align: center

TLS 检查器过滤器实现 :repo:`ListenerFilter <include/envoy/network/filter.h>` 接口。所有过滤器接口，无论是监听器还是网络/HTTP，都要求过滤器实现特定连接或流事件的回调。
在 ListenerFilter 的情况下为：

.. code-block:: cpp

  virtual FilterStatus onAccept(ListenerFilterCallbacks& cb) PURE;

``onAccept()`` 允许筛选器在 TCP 接受处理期间运行。通过回调返回的 ``FilterStatus`` 来控制监听过滤链将如何继续工作。监听过滤器可以暂停过滤器链，然后稍后恢复，例如：响应对另一个服务进行的 RPC 。

从监听听过滤器和连接属性中提取的信息用于匹配过滤器链，从而提供网络过滤器链和将用于处理连接的传输套接字。

.. image:: /_static/lor-filter-chain-match.svg
   :width: 50%
   :align: center

.. _life_of_a_request_tls_decryption:

3. TLS 传输套接字解密
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Envoy offers pluggable transport sockets via the
:repo:`TransportSocket <include/envoy/network/transport_socket.h>`
extension interface. Transport sockets follow the lifecycle events of a TCP connection and
read/write into network buffers. Some key methods that transport sockets must implement are:

.. code-block:: cpp

  virtual void onConnected() PURE;
  virtual IoResult doRead(Buffer::Instance& buffer) PURE;
  virtual IoResult doWrite(Buffer::Instance& buffer, bool end_stream) PURE;
  virtual void closeSocket(Network::ConnectionEvent event) PURE;

When data is available on a TCP connection, ``Network::ConnectionImpl::onReadReady()`` invokes the
:ref:`TLS <arch_overview_ssl>` transport socket via ``SslSocket::doRead()``. The transport socket
then performs a TLS handshake on the TCP connection. When the handshake completes,
``SslSocket::doRead()`` provides a decrypted byte stream to an instance of
``Network::FilterManagerImpl``, responsible for managing the network filter chain.

.. image:: /_static/lor-transport-socket.svg
   :width: 80%
   :align: center

It’s important to note that no operation, whether it’s a TLS handshake or a pause of a filter
pipeline is truly blocking. Since Envoy is event-based, any situation in which processing requires
additional data will lead to early event completion and yielding of the CPU to another event. When
the network makes more data available to read, a read event will trigger the resumption of a TLS
handshake.

4. 网络过滤器链处理
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

As with the listener filter chain, Envoy, via `Network::FilterManagerImpl`, will instantiate a
series of :ref:`network filters <arch_overview_network_filters>` from their filter factories. The
instance is fresh for each new connection. Network filters, like transport sockets, follow TCP
lifecycle events and are invoked as data becomes available from the transport socket.

.. image:: /_static/lor-network-filters.svg
   :width: 80%
   :align: center

Network filters are composed as a pipeline, unlike transport sockets which are one-per-connection.
Network filters come in three varieties:

* :repo:`ReadFilter <include/envoy/network/filter.h>` implementing ``onData()``, called when data is
  available from the connection (due to some request).
* :repo:`WriteFilter <include/envoy/network/filter.h>` implementing ``onWrite()``, called when data
  is about to be written to the connection (due to some response).
* :repo:`Filter <include/envoy/network/filter.h>` implementing both *ReadFilter* and *WriteFilter*.

The method signatures for the key filter methods are:

.. code-block:: cpp

  virtual FilterStatus onNewConnection() PURE;
  virtual FilterStatus onData(Buffer::Instance& data, bool end_stream) PURE;
  virtual FilterStatus onWrite(Buffer::Instance& data, bool end_stream) PURE;

As with the listener filter, the ``FilterStatus`` allows filters to pause execution of the filter
chain. For example, if a rate limiting service needs to be queried, a rate limiting network filter
would return ``Network::FilterStatus::StopIteration`` from ``onData()`` and later invoke
``continueReading()`` when the query completes.

The last network filter for a listener dealing with HTTP is :ref:`HTTP connection manager
<arch_overview_http_conn_man>` (HCM). This is responsible for creating the HTTP/2 codec and managing
the HTTP filter chain. In our example, this is the only network filter. An example network filter
chain making use of multiple network filters would look like:

.. image:: /_static/lor-network-read.svg
   :width: 80%
   :align: center

On the response path, the network filter chain is executed in the reverse order to the request path.

.. image:: /_static/lor-network-write.svg
   :width: 80%
   :align: center

.. _life_of_a_request_http2_decoding:

5. HTTP/2 编解码器解码
^^^^^^^^^^^^^^^^^^^^^^^^

The HTTP/2 codec in Envoy is based on `nghttp2 <https://nghttp2.org/>`_. It is invoked by the HCM
with plaintext bytes from the TCP connection (after network filter chain transformation). The codec
decodes the byte stream as a series of HTTP/2 frames and demultiplexes the connection into a number
of independent HTTP streams. Stream multiplexing is a key feature in HTTP/2, providing significant
performance advantages over HTTP/1. Each HTTP stream handles a single request and response.

The codec is also responsible for handling HTTP/2 setting frames and both stream and connection
level :repo:`flow control <source/docs/flow_control.md>`.

The codecs are responsible for abstracting the specifics of the HTTP connection, presenting a
standard view to the HTTP connection manager and HTTP filter chain of a connection split into
streams, each with request/response headers/body/trailers. This is true regardless of whether the
protocol is HTTP/1, HTTP/2 or HTTP/3.

6. HTTP 过滤器链处理
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

For each HTTP stream, the HCM instantiates an :ref:`HTTP filter <arch_overview_http_filters>` chain,
following the pattern established above for listener and network filter chains.

.. image:: /_static/lor-http-filters.svg
   :width: 80%
   :align: center

There are three kinds of HTTP filter interfaces:

* :repo:`StreamDecoderFilter <include/envoy/http/filter.h>` with callbacks for request processing.
* :repo:`StreamEncoderFilter <include/envoy/http/filter.h>` with callbacks for response processing.
* :repo:`StreamFilter <include/envoy/http/filter.h>` implementing both `StreamDecoderFilter` and
  `StreamEncoderFilter`.

Looking at the decoder filter interface:

.. code-block:: cpp

  virtual FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) PURE;
  virtual FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) PURE;
  virtual FilterTrailersStatus decodeTrailers(RequestTrailerMap& trailers) PURE;

Rather than operating on connection buffers and events, HTTP filters follow the lifecycle of an HTTP
request, e.g. ``decodeHeaders()`` takes HTTP headers as an argument rather than a byte buffer. The
returned ``FilterStatus`` provides, as with network and listener filters, the ability to manage filter
chain control flow.

When the HTTP/2 codec makes available the HTTP requests headers, these are first passed to
``decodeHeaders()`` in CustomFilter. If the returned ``FilterHeadersStatus`` is ``Continue``, HCM
then passes the headers (possibly mutated by CustomFilter) to the router filter.

Decoder and encoder-decoder filters are executed on the request path. Encoder and encoder-decoder
filters are executed on the response path, in :ref:`reverse direction
<arch_overview_http_filters_ordering>`. Consider the following example filter chain:

.. image:: /_static/lor-http.svg
   :width: 80%
   :align: center

The request path will look like:

.. image:: /_static/lor-http-decode.svg
   :width: 80%
   :align: center

While the response path will look like:

.. image:: /_static/lor-http-encode.svg
   :width: 80%
   :align: center

When ``decodeHeaders()`` is invoked on the :ref:`router <arch_overview_http_routing>` filter, the
route selection is finalized and a cluster is picked. The HCM selects a route from its
``RouteConfiguration`` at the start of HTTP filter chain execution. This is referred to as the
*cached route*. Filters may modify headers and cause a new route to be selected, by asking HCM to
clear the route cache and requesting HCM to reevaluate the route selection. When the router filter
is invoked, the route is finalized. The selected route’s configuration will point at an upstream
cluster name. The router filter then asks the `ClusterManager` for an HTTP :ref:`connection pool
<arch_overview_conn_pool>` for the cluster. This involves load balancing and the connection pool,
discussed in the next section.

.. image:: /_static/lor-route-config.svg
   :width: 70%
   :align: center

The resulting HTTP connection pool is used to build an `UpstreamRequest` object in the router, which
encapsulates the HTTP encoding and decoding callback methods for the upstream HTTP request. Once a
stream is allocated on a connection in the HTTP connection pool, the request headers are forwarded
to the upstream endpoint by the invocation of ``UpstreamRequest::encoderHeaders()``.

The router filter is responsible for all aspects of upstream request lifecycle management on the
stream allocated from the HTTP connection pool. It also is responsible for request timeouts, retries
and affinity.

7. 负载均衡
^^^^^^^^^^^^^^^^^

Each cluster has a :ref:`load balancer <arch_overview_load_balancing>` which picks an endpoint when
a new request arrives. Envoy supports a variety of load balancing algorithms, e.g. weighted
round-robin, Maglev, least-loaded, random. Load balancers obtain their effective assignments from a
combination of static bootstrap configuration, DNS, dynamic xDS (the CDS and EDS discovery services)
and active/passive health checks. Further details on how load balancing works in Envoy are provided
in the :ref:`load balancing documentation <arch_overview_load_balancing>`.

Once an endpoint is selected, the :ref:`connection pool <arch_overview_conn_pool>` for this endpoint
is used to find a connection to forward the request on. If no connection to the host exists, or all
connections are at their maximum concurrent stream limit, a new connection is established and placed
in the connection pool, unless the circuit breaker for maximum connections for the cluster has
tripped. If a maximum lifetime stream limit for a connection is configured and reached, a new
connection is allocated in the pool and the affected HTTP/2 connection is drained. Other circuit
breakers, e.g. maximum concurrent requests to a cluster are also checked. See :repo:`circuit
breakers <arch_overview_circuit_breakers>` and :ref:`connection pools <arch_overview_conn_pool>` for
further details.

.. image:: /_static/lor-lb.svg
   :width: 80%
   :align: center

8. HTTP/2 编解码器编码
^^^^^^^^^^^^^^^^^^^^^^^^

The selected connection's HTTP/2 codec multiplexes the request stream with any other streams going
to the same upstream over a single TCP connection. This is the reverse of :ref:`HTTP/2 codec
decoding <life_of_a_request_http2_decoding>`.

As with the downstream HTTP/2 codec, the upstream codec is responsible for taking Envoy’s standard
abstraction of HTTP, i.e. multiple streams multiplexed on a single connection with request/response
headers/body/trailers, and mapping this to the specifics of HTTP/2 by generating a series of HTTP/2
frames.

9. TLS 传输套接字加密
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The upstream endpoint connection's TLS transport socket encrypts the bytes from the HTTP/2 codec
output and writes them to a TCP socket for the upstream connection. As with :ref:`TLS transport
socket decryption <life_of_a_request_tls_decryption>`, in our example the cluster has a transport
socket configured that provides TLS transport security. The same interfaces exist for upstream and
downstream transport socket extensions.

.. image:: /_static/lor-client.svg
   :width: 70%
   :align: center

10. 响应路径和 HTTP 生命周期
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The request, consisting of headers, and optional body and trailers, is proxied upstream, and the
response is proxied downstream. The response passes through the HTTP and network filters in the
:ref:`opposite order <arch_overview_http_filters_ordering>`. from the request.

Various callbacks for decoder/encoder request lifecycle events will be invoked in HTTP filters, e.g.
when response trailers are being forwarded or the request body is streamed. Similarly, read/write
network filters will also have their respective callbacks invoked as data continues to flow in both
directions during a request.

:ref:`Outlier detection <arch_overview_outlier_detection>` status for the endpoint is revised as the
request progresses.

A request completes when the upstream response reaches its end-of-stream, i.e. when trailers or the
response header/body with end-stream set are received. This is handled in
``Router::Filter::onUpstreamComplete()``.

It is possible for a request to terminate early. This may be due to (but not limited to):

* Request timeout.
* Upstream endpoint steam reset.
* HTTP filter stream reset.
* Circuit breaking.
* Unavailability of upstream resources, e.g. missing a cluster for a route.
* No healthy endpoints.
* DoS protection.
* HTTP protocol violations.
* Local reply from either the HCM or an HTTP filter. E.g. a rate limit HTTP filter returning a 429
  response.

If any of these occur, Envoy may either send an internally generated response, if upstream response
headers have not yet been sent, or will reset the stream, if response headers have already been
forwarded downstream. The Envoy :ref:`debugging FAQ <faq_overview_debug>` has further information on
interpreting these early stream terminations.

11. 请求后处理过程
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Once a request completes, the stream is destroyed. The following also takes places:

* The post-request :ref:`statistics <arch_overview_statistics>` are updated (e.g. timing, active
  requests, upgrades, health checks). Some statistics are updated earlier however, during request
  processing. Stats are not written to the stats :ref:`sink
  <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_sinks>` at this point, they are batched
  and written by the main thread periodically. In our example this is a statsd sink.
* :ref:`Access logs <arch_overview_access_logs>` are written to the access log :ref:`sinks
  <arch_overview_access_logs_sinks>`. In our example this is a file access log.
* :ref:`Trace <arch_overview_tracing>` spans are finalized. If our example request was traced, a
  trace span, describing the duration and details of the request would be created by the HCM when
  processing request headers and then finalized by the HCM during post-request processing.
