.. _arch_overview_http_conn_man:

HTTP connection management
==========================

HTTP is such a critical component of modern service oriented architectures that Envoy implements a
large amount of HTTP specific functionality. Envoy has a built in network level filter called the
:ref:`HTTP connection manager <config_http_conn_man>`. This filter translates raw bytes into HTTP
level messages and events (e.g., headers received, body data received, trailers received, etc.). It
also handles functionality common to all HTTP connections and requests such as :ref:`access logging
<arch_overview_access_logs>`, :ref:`request ID generation and tracing <arch_overview_tracing>`,
:ref:`request/response header manipulation <config_http_conn_man_headers>`, :ref:`route table
<arch_overview_http_routing>` management, and :ref:`statistics <config_http_conn_man_stats>`.

HTTP connection manager :ref:`configuration <config_http_conn_man>`.

.. _arch_overview_http_protocols:

HTTP protocols
--------------

Envoy’s HTTP connection manager has native support for HTTP/1.1, WebSockets, and HTTP/2. It does not support
SPDY. Envoy’s HTTP support was designed to first and foremost be an HTTP/2 multiplexing proxy.
Internally, HTTP/2 terminology is used to describe system components. For example, an HTTP request
and response take place on a *stream*. A codec API is used to translate from different wire
protocols into a protocol agnostic form for streams, requests, responses, etc. In the case of
HTTP/1.1, the codec translates the serial/pipelining capabilities of the protocol into something
that looks like HTTP/2 to higher layers. This means that the majority of the code does not need to
understand whether a stream originated on an HTTP/1.1 or HTTP/2 connection.

HTTP header sanitizing
----------------------

The HTTP connection manager performs various :ref:`header sanitizing
<config_http_conn_man_header_sanitizing>` actions for security reasons.

Route table configuration
-------------------------

Each :ref:`HTTP connection manager filter <config_http_conn_man>` has an associated :ref:`route
table <arch_overview_http_routing>`. The route table can be specified in one of two ways:

* Statically.
* Dynamically via the :ref:`RDS API <config_http_conn_man_rds>`.

Timeouts
--------

Various configurable timeouts apply to an HTTP connection and its constituent streams:

* Connection-level :ref:`idle timeout
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.idle_timeout>`:
  this applies to the idle period where no streams are active.
* Connection-level :ref:`drain timeout
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.drain_timeout>`:
  this spans between an Envoy originated GOAWAY and connection termination.
* Stream-level idle timeout: this applies to each individual stream. It may be configured at both
  the :ref:`connection manager
  <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stream_idle_timeout>`
  and :ref:`per-route <envoy_api_field_route.RouteAction.idle_timeout>` granularity.
  Header/data/trailer events on the stream reset the idle timeout.
* Stream-level :ref:`per-route upstream timeout <envoy_api_field_route.RouteAction.timeout>`: this
  applies to the upstream response, i.e. a maximum bound on the time from the end of the downstream
  request until the end of the upstream response. This may also be specified at the :ref:`per-retry
  <envoy_api_field_route.RouteAction.RetryPolicy.per_try_timeout>` granularity.
* Stream-level :ref:`per-route gRPC max timeout
  <envoy_api_field_route.RouteAction.max_grpc_timeout>`: this bounds the upstream timeout and allows
  the timeout to be overriden via the *grpc-timeout* request header.
