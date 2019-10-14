How do I configure timeouts?
============================

Envoy supports a wide range of timeouts that may need to be configured depending on the deployment.
This page summarizes the most important timeouts used in various scenarios.

.. attention::

  This is not an exhaustive list of all of the configurable timeouts that Envoy supports. Depending
  on the deployment additional configuration may be required.

HTTP/gRPC
---------

Connection timeouts
^^^^^^^^^^^^^^^^^^^

* The HTTP protocol :ref:`idle timeout <envoy_api_field_core.HttpProtocolOptions.idle_timeout>`
  is defined in a generic message used by both the HTTP connection manager as well as upstream
  cluster HTTP connections. The idle timeout is the time at which a downstream or upstream
  connection will be terminated if there are no active streams. The default idle timeout if not
  otherwise specified is *1 hour*. To modify the idle timeout for downstream connections use the
  :ref:`common_http_protocol_options
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.common_http_protocol_options>`
  field in the HTTP connection manager configuration. To modify the idle timeout for upstream
  connections use the
  :ref:`common_http_protocol_options <envoy_api_field_Cluster.common_http_protocol_options>` field
  in the cluster configuration.
* The HTTP connection manager :ref:`request_timeout
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.request_timeout>`
  is the amount of time the connection manager will allow for the *entire request* to be received
  by the client.

  .. attention::

    This timeout has no default value as it is not compatible with streaming requests (requests that
    never end). See the stream idle timeout that follows. However, if using the :ref:`buffer filter
    <config_http_filters_buffer>`, it is recommended to configure this timeout.
* The HTTP connection manager :ref:`stream_idle_timeout
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stream_idle_timeout>`
  is the amount of time that the connection manager will allow a stream to exist with no upstream
  or downstream activity. The default stream idle timeout if not otherwise specified is *5 minutes*.
  This timeout is strongly recommended for streaming APIs.

Route timeouts
^^^^^^^^^^^^^^

* A route :ref:`timeout <envoy_api_field_route.RouteAction.timeout>` is the amount of time that
  Envoy will wait for the upstream to respond with a complete response. This timeout does not
  start until the entire downstream request has been received.

  .. attention::

    This timeout defaults to *15 seconds*, however, it is not compatible with streaming responses
    (responses that never end), and will need to be disabled. Stream idle timeouts should be used
    in the case of streaming APIs as described elsewhere on this page.
* The route :ref:`idle_timeout <envoy_api_field_route.RouteAction.idle_timeout>` allows overriding
  of the HTTP connection manager :ref:`stream_idle_timeout
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stream_idle_timeout>`
  and does the same thing.
* The route :ref:`per_try_timeout <envoy_api_field_route.RetryPolicy.per_try_timeout>` can be
  configured when using retries so that individual tries using a shorter timeout than the overall
  request timeout described above. This type of timeout will not work with streaming APIs (in which
  retries are typically not possible) but is useful for decreasing the tail latency of non-streaming
  APIs.

TCP
---

* The cluster :ref:`connect_timeout <envoy_api_field_Cluster.connect_timeout>` specifies the amount
  of time Envoy will wait for an upstream TCP connection to be established. This timeout has no
  default, but is required in the configuration.

  .. attention::

    For TLS connections, the connect timeout includes the TLS handshake.
* The TCP proxy :ref:`idle_timeout
  <envoy_api_field_config.filter.network.tcp_proxy.v2.TcpProxy.idle_timeout>`
  is the amount of time that the TCP proxy will allow a connection to exist with no upstream
  or downstream activity. This timeout currently has no default value but configuring it is
  strongly recommended.
