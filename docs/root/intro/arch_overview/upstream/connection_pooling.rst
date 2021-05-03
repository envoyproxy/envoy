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
imposed by :ref:`max concurrent streams
<envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_concurrent_streams>` and :ref:`max
requests per connection <envoy_v3_api_field_config.cluster.v3.Cluster.max_requests_per_connection>`.
The HTTP/2 connection pool establishes as many connections as are needed to serve requests. With no
limits, this will be only a single connection. If a GOAWAY frame is received or if the connection
reaches the :ref:`maximum requests per connection
<envoy_v3_api_field_config.cluster.v3.Cluster.max_requests_per_connection>` limit, the connection
pool will drain the affected connection. Once a connection reaches its :ref:`maximum concurrent
stream limit <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_concurrent_streams>`, it
will be marked as busy until a stream is available. New connections are established anytime there is
a pending request without a connection that can be dispatched to (up to circuit breaker limits for
connections). HTTP/2 is the preferred communication protocol, as connections rarely, if ever, get
severed.

.. _arch_overview_conn_pool_how_many:

Number of connection pools
--------------------------

Each host in each cluster will have one or more connection pools. If the cluster is HTTP/1 or HTTP/2
only, then the host may have only a single connection pool. However, if the cluster supports multiple
upstream protocols, then at least one connection pool per protocol will be allocated. Separate
connection pools are also allocated for each of the following features:

* :ref:`Routing priority <arch_overview_http_routing_priority>`
* :ref:`Socket options <envoy_v3_api_field_config.core.v3.BindConfig.socket_options>`
* :ref:`Transport socket (e.g. TLS) options <envoy_v3_api_msg_config.core.v3.TransportSocket>`

Each worker thread maintains its own connection pools for each cluster, so if an Envoy has two
threads and a cluster with both HTTP/1 and HTTP/2 support, there will be at least 4 connection pools.

.. _arch_overview_conn_pool_health_checking:

Health checking interactions
----------------------------

If Envoy is configured for either active or passive :ref:`health checking
<arch_overview_health_checking>`, all connection pool connections will be closed on behalf of a host
that transitions from an available state to an unavailable state. If the host reenters the load
balancing rotation it will create fresh connections which will maximize the chance of working
around a bad flow (due to ECMP route or something else).
