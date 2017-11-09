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

The HTTP/2 connection pool acquires a single connection to an upstream host. All requests are
multiplexed over this connection. If a GOAWAY frame is received or if the connection reaches the
maximum stream limit, the connection pool will create a new connection and drain the existing one.
HTTP/2 is the preferred communication protocol as connections rarely if ever get severed.

.. _arch_overview_conn_pool_health_checking:

Health checking interactions
----------------------------

If Envoy is configured for either active or passive :ref:`health checking
<arch_overview_health_checking>`, all connection pool connections will be closed on behalf of a host
that transitions from a healthy state to an unhealthy state. If the host reenters the load
balancing rotation it will create fresh connections which will maximize the chance of working
around a bad flow (due to ECMP route or something else).
