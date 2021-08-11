.. _arch_overview_conn_pool:

Connection pooling
==================

For HTTP traffic, Envoy supports abstract connection pools that are layered on top of the underlying
wire protocol (HTTP/1.1, HTTP/2, HTTP/3). The utilizing filter code does not need to be aware of whether
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
connections). HTTP/2 is the preferred communication protocol when Envoy is operating as a reverse proxy,
as connections rarely, if ever, get severed.

HTTP/3
------

The HTTP/3 connection pool multiplexes multiple requests over a single connection, up to the limits
imposed by :ref:`max concurrent streams
<envoy_v3_api_field_config.core.v3.QuicProtocolOptions.max_concurrent_streams>` and :ref:`max
requests per connection <envoy_v3_api_field_config.cluster.v3.Cluster.max_requests_per_connection>`.
The HTTP/3 connection pool establishes as many connections as are needed to serve requests. With no
limits, this will be only a single connection. If a GOAWAY frame is received or if the connection
reaches the :ref:`maximum requests per connection
<envoy_v3_api_field_config.cluster.v3.Cluster.max_requests_per_connection>` limit, the connection
pool will drain the affected connection. Once a connection reaches its :ref:`maximum concurrent
stream limit <envoy_v3_api_field_config.core.v3.QuicProtocolOptions.max_concurrent_streams>`, it
will be marked as busy until a stream is available. New connections are established anytime there is
a pending request without a connection that can be dispatched to (up to circuit breaker limits for
connections). HTTP/3 upstream support is currently only usable in situations where HTTP/3 is guaranteed
to work, but automatic failover to TCP is coming soon!.

Automatic protocol selection
----------------------------

For Envoy acting as a forward proxy, the preferred configuration is the
`AutoHttpConfig <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions.AutoHttpConfig>`
, configued via
`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>`.
By default it will use TCP and ALPN to select the best available protocol of HTTP/2 and HTTP/1.1.

.. _arch_overview_http3_upstream:

If HTTP/3 is configured in the automatic pool it will currently attempt an QUIC connection first,
then 300ms later, if a QUIC connection is not established, will also attempt to establish a TCP connection.
Whichever handshake succeeds will be used for the initial
stream, but if both TCP and QUIC connections are established, QUIC will eventually be preferred.

Upcoming versions of HTTP/3 support will include only selecting HTTP/3 if the upstream advertises support
either via `HTTP Alternative Services <https://tools.ietf.org/html/rfc7838>`_,
`HTTPS DNS RR <https://datatracker.ietf.org/doc/html/draft-ietf-dnsop-svcb-https-04>`_, or "QUIC hints" which
will be manually configured. This path is alpha and rapidly undergoing improvements with the goal of having
the default behavior result in optimal latency for internet environments, so please be patient and follow along with Envoy release notes
to stay aprised of the latest and greatest changes.


.. _arch_overview_conn_pool_how_many:

Number of connection pools
--------------------------

Each host in each cluster will have one or more connection pools. If the cluster has a single explicit
protocol configured, then the host may have only a single connection pool. However, if the cluster supports multiple
upstream protocols, then unless it is using ALPN, one connection pool per protocol may be allocated. Separate
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
