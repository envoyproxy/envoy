.. _faq_configuration_timeouts:

How do I configure timeouts?
============================

Envoy supports a wide range of timeouts that may need to be configured depending on the deployment.
This page summarizes the most important timeouts used in various scenarios.

.. attention::

  This is not an exhaustive list of all of the configurable timeouts that Envoy supports. Depending
  on the deployment additional configuration may be required.

HTTP/gRPC
---------

.. _faq_configuration_connection_timeouts:

Connection timeouts
^^^^^^^^^^^^^^^^^^^

Connection timeouts apply to the entire HTTP connection and all streams the connection carries.

* The HTTP protocol :ref:`idle_timeout <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.idle_timeout>`
  is defined in a generic message used by both the HTTP connection manager as well as upstream
  cluster HTTP connections. The idle timeout is the time at which a downstream or upstream
  connection will be terminated if there are no active streams. The default idle timeout if not
  otherwise specified is *1 hour*. To modify the idle timeout for downstream connections use the
  :ref:`common_http_protocol_options
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.common_http_protocol_options>`
  field in the HTTP connection manager configuration. To modify the idle timeout for upstream
  connections use the
  :ref:`common_http_protocol_options <envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.common_http_protocol_options>` field in the Cluster's :ref:`extension_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.typed_extension_protocol_options>`, keyed by :ref:`HttpProtocolOptions <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>`

* The HTTP protocol :ref:`max_connection_duration <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_connection_duration>`
  is defined in a generic message used by both the HTTP connection manager as well as upstream cluster
  HTTP connections. The maximum connection duration is the time after which a downstream or upstream
  connection will be drained and/or closed, starting from when it was first established. If there are no
  active streams, the connection will be closed. If there are any active streams, the drain sequence will
  kick-in, and the connection will be force-closed after the drain period. The default value of max connection
  duration is *0* or unlimited, which means that the connections will never be closed due to aging. It could
  be helpful in scenarios when you are running a pool of Envoy edge-proxies and would want to close a
  downstream connection after some time to prevent stickiness. It could also help to better load balance the
  overall traffic among this pool, especially if the size of this pool is dynamically changing. Finally, it
  may help with upstream connections when using a DNS name whose resolved addresses may change even if the
  upstreams stay healthly. Forcing a maximum upstream lifetime in this scenario prevents holding onto healthy
  connections even after they would otherwise be undiscoverable. To modify the max connection duration for downstream connections use the
  :ref:`common_http_protocol_options <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.common_http_protocol_options>`
  field in the HTTP connection manager configuration. To modify the max connection duration for upstream connections use the
  :ref:`common_http_protocol_options <envoy_v3_api_field_config.cluster.v3.Cluster.common_http_protocol_options>` field in the cluster configuration.

See :ref:`below <faq_configuration_timeouts_transport_socket>` for other connection timeouts.

Stream timeouts
^^^^^^^^^^^^^^^

Stream timeouts apply to individual streams carried by an HTTP connection. Note that a stream is
an HTTP/2 and HTTP/3 concept, however internally Envoy maps HTTP/1 requests to streams so in this
context request/stream is interchangeable.

* The HTTP connection manager :ref:`request_timeout
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.request_timeout>`
  is the amount of time the connection manager will allow for the *entire request stream* to be
  received from the client.

  .. attention::

    This timeout is not enforced by default as it is not compatible with streaming requests
    (requests that never end). See the stream idle timeout that follows. However, if using the
    :ref:`buffer filter <config_http_filters_buffer>`, it is recommended to configure this timeout.
* The HTTP connection manager :ref:`request_headers_timeout
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.request_headers_timeout>`
  determines the amount of time the client has to send *only the headers* on the request stream
  before the stream is cancelled. This can be used to prevent clients from consuming too much
  memory by creating large numbers of mostly-idle streams waiting for headers. The request header
  timeout is disabled by default.
* The HTTP connection manager :ref:`stream_idle_timeout
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stream_idle_timeout>`
  is the amount of time that the connection manager will allow a stream to exist with no upstream
  or downstream activity. The default stream idle timeout is *5 minutes*. This timeout is strongly
  recommended for all requests (not just streaming requests/responses) as it additionally defends
  against a peer that does not open the stream window once an entire response has been buffered
  to be sent to a downstream client.
* The HTTP protocol :ref:`max_stream_duration <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_stream_duration>`
  is defined in a generic message used by the HTTP connection manager. The max stream duration is the
  maximum time that a stream's lifetime will span. You can use this functionality when you want to reset
  HTTP request/response streams periodically. You can't use :ref:`request_timeout
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.request_timeout>`
  in this situation because this timer will be disarmed if a response header is received on the request/response streams.
  This timeout is available on both upstream and downstream connections.

Route timeouts
^^^^^^^^^^^^^^

Envoy supports additional stream timeouts at the route level, as well as overriding some of the
stream timeouts already introduced above.

* A route :ref:`timeout <envoy_v3_api_field_config.route.v3.RouteAction.timeout>` is the amount of time that
  Envoy will wait for the upstream to respond with a complete response. *This timeout does not
  start until the entire downstream request stream has been received*.

  .. attention::

    This timeout defaults to *15 seconds*, however, it is not compatible with streaming responses
    (responses that never end), and will need to be disabled. Stream idle timeouts should be used
    in the case of streaming APIs as described elsewhere on this page.
* The route :ref:`idle_timeout <envoy_v3_api_field_config.route.v3.RouteAction.idle_timeout>` allows overriding
  of the HTTP connection manager :ref:`stream_idle_timeout
  <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stream_idle_timeout>`
  and does the same thing.
* The route :ref:`per_try_timeout <envoy_v3_api_field_config.route.v3.RetryPolicy.per_try_timeout>` can be
  configured when using retries so that individual tries using a shorter timeout than the overall
  request timeout described above. This timeout only applies before any part of the response
  is sent to the downstream, which normally happens after the upstream has sent response headers.
  This timeout can be used with streaming endpoints to retry if the upstream fails to begin a
  response within the timeout.
* The route :ref:`per_try_idle_timeout <envoy_v3_api_field_config.route.v3.RetryPolicy.per_try_idle_timeout>`
  can be configured to ensure continued response progress of individual retry attempts (including
  the first attempt). This is useful in cases where the total upstream request time is bounded
  by the number of attempts multiplied by the per try timeout, but while the user wants to
  ensure that individual attempts are making progress.
* The route :ref:`MaxStreamDuration proto <envoy_v3_api_msg_config.route.v3.RouteAction.MaxStreamDuration>`
  can be used to override the HttpConnectionManager's
  :ref:`max_stream_duration <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_stream_duration>`
  for individual routes as well as setting both limits and a fixed time offset on grpc-timeout headers.

Scaled timeouts
^^^^^^^^^^^^^^^

In situations where envoy is under high load, Envoy can dynamically configure timeouts using scaled timeouts.
Envoy supports scaled timeouts through the :ref:`Overload Manager <envoy_v3_api_msg_config.overload.v3.OverloadManager>`, configured
in envoy :ref:`bootstrap configuration <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.overload_manager>`.
Using a :ref:`reduce timeouts <config_overload_manager_reducing_timeouts>` overload action,
the Overload Manager can be configured to monitor :ref:`resources <envoy_v3_api_msg_config.overload.v3.ResourceMonitor>`
and scale timeouts accordingly. For example, a common use case may be to monitor the Envoy :ref:`heap size <envoy_v3_api_msg_extensions.resource_monitors.fixed_heap.v3.FixedHeapConfig>`
and set the scaled TimerType to :ref:`HTTP_DOWNSTREAM_CONNECTION_IDLE <envoy_v3_api_enum_value_config.overload.v3.ScaleTimersOverloadActionConfig.TimerType.HTTP_DOWNSTREAM_CONNECTION_IDLE>`.
The overload manager will scale down the :ref:`idle timeout <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.idle_timeout>` once the :ref:`scaling_threshold <envoy_v3_api_field_config.overload.v3.ScaledTrigger.scaling_threshold>` has been met
and will set the timeout to the :ref:`min timeout <envoy_v3_api_field_config.overload.v3.ScaleTimersOverloadActionConfig.ScaleTimer.min_timeout>` once the :ref:`scaling_threshold <envoy_v3_api_field_config.overload.v3.ScaledTrigger.scaling_threshold>` is met.
The full list of supported timers that can be scaled is available in the overload manager :ref:`docs <envoy_v3_api_enum_config.overload.v3.ScaleTimersOverloadActionConfig.TimerType>`.

TCP
---

* The cluster :ref:`connect_timeout <envoy_v3_api_field_config.cluster.v3.Cluster.connect_timeout>` specifies the amount
  of time Envoy will wait for an upstream TCP connection to be established. If this value is not set,
  a default value of 5 seconds will be used.

  .. attention::

    For upstream TLS connections, the connect timeout includes the TLS handshake. For downstream
    connections, see :ref:`below <faq_configuration_timeouts_transport_socket>` for configuration options.

* The TCP proxy :ref:`idle_timeout
  <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.idle_timeout>`
  is the amount of time that the TCP proxy will allow a connection to exist with no upstream
  or downstream activity. The default idle timeout if not otherwise specified is *1 hour*.

* The TCP protocol :ref:`idle_timeout <envoy_v3_api_field_extensions.upstreams.tcp.v3.TcpProtocolOptions.idle_timeout>`
  is defined in a :ref:`TcpProtocolOptions <envoy_v3_api_msg_extensions.upstreams.tcp.v3.TcpProtocolOptions>`
  used by all TCP connections from pool. The idle timeout is the time at which
  a upstream connection will be terminated if the connection is not associated with a downstream connection.
  This defaults to *10 minutes*. To disable idle timeouts, explicitly set
  :ref:`idle_timeout <envoy_v3_api_field_extensions.upstreams.tcp.v3.TcpProtocolOptions.idle_timeout>` to 0.

.. _faq_configuration_timeouts_transport_socket:

Transport Socket
----------------

* The :ref:`transport_socket_connect_timeout <envoy_v3_api_field_config.listener.v3.FilterChain.transport_socket_connect_timeout>`
  specifies the amount of time Envoy will wait for a downstream client to complete transport-level
  negotiations. When configured on a filter chain with a TLS or ALTS transport socket, this limits
  the amount of time allowed to finish the encrypted handshake after establishing a TCP connection.
