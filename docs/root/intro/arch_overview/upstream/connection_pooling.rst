.. _arch_overview_conn_pool:

Connection pooling
==================

For HTTP traffic, Envoy supports abstract connection pools that are layered on top of the underlying
wire protocol (HTTP/1.1 or HTTP/2). The utilizing filter code does not need to be aware of whether
the underlying protocol supports true multiplexing or not. In practice the underlying
implementations have the following high level properties:

HTTP/1.1
--------

The HTTP/1.1 connection pool acquires connections as needed to an upstream host (up to the circuit
breaking limit). Requests are bound to connections as they become available, either because a
connection is done processing a previous request or because a new connection is ready to receive its
first request. The HTTP/1.1 connection pool does not make use of pipelining so that only a single
downstream request must be reset if the upstream connection is severed.

HTTP/2
------

The HTTP/2 connection pool multiplexes multiple requests over a single connection, up to the limits
imposed by :ref:`max concurrent streams <envoy_api_field_core.Http2ProtocolOptions.max_concurrent_streams>`
and :ref:`max requests per connection <envoy_api_field_Cluster.max_requests_per_connection>`.
The HTTP/2 connection pool establishes only as many connections as are needed to serve the current
requests. With no limits, this will be only a single connection. If a GOAWAY frame is received or
if the connection reaches the maximum stream limit, the connection pool will drain the existing one.
New connections are established anytime there is a pending request without a connection that it can
be dispatched to (up to circuit breaker limits for connections).
HTTP/2 is the preferred communication protocol as connections rarely if ever get severed.

.. _arch_overview_conn_pool_health_checking:

Health checking interactions
----------------------------

If Envoy is configured for either active or passive :ref:`health checking
<arch_overview_health_checking>`, all connection pool connections will be closed on behalf of a host
that transitions from a available state to an unavailable state. If the host reenters the load
balancing rotation it will create fresh connections which will maximize the chance of working
around a bad flow (due to ECMP route or something else).
