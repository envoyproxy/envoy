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
connections).

Automatic protocol selection
----------------------------

For Envoy acting as a forward proxy, the preferred configuration is the
:ref:`AutoHttpConfig <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions.AutoHttpConfig>`
, configued via
:ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>`.
By default it will use TCP and ALPN to select the best available protocol of HTTP/2 and HTTP/1.1.

.. _arch_overview_http3_pooling_upstream:

For auto-http with HTTP/3, an alternate protocol cache must be configured via
:ref:`alternate_protocols_cache_options <envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.AutoHttpConfig.alternate_protocols_cache_options>`.  HTTP/3 connections will only be attempted to servers which
advertise HTTP/3 support either via `HTTP Alternative Services <https://tools.ietf.org/html/rfc7838>`_, (eventually
the `HTTPS DNS resource record <https://datatracker.ietf.org/doc/html/draft-ietf-dnsop-svcb-https-04>`_ or "QUIC hints"
which will be manually configured).
If no such advertisement exists, then HTTP/2 or HTTP/1 will be used instead.

When HTTP/3 is attempted, Envoy will currently attempt an QUIC connection first,
then 300ms later, if a QUIC connection is not established, will also attempt to establish a TCP connection.
Whichever handshake succeeds will be used for the initial
stream, but if both TCP and QUIC connections are established, QUIC will eventually be preferred.

Further as HTTP/3 runs over QUIC (which uses UDP) and not over TCP (which HTTP/1 and HTTP/2 use).
It is not uncommon for network devices to block UDP traffic, and hence block HTTP/3. This
means that upstream HTTP/3 connection attempts might be blocked by the network and will fall
back to using HTTP/2 or HTTP/1.  This code path is still considered alpha until it has significant
production burn time, but is considered ready for use.

.. _arch_overview_happy_eyeballs:

Happy Eyeballs Support
----------------------

Envoy supports Happy Eyeballs, `RFC8305 <https://tools.ietf.org/html/rfc8305>`_,
for upstream TCP connections. For clusters which use
:ref:`LOGICAL_DNS<envoy_v3_api_enum_value_config.cluster.v3.Cluster.DiscoveryType.LOGICAL_DNS>`,
this behavior is configured by setting the DNS IP address resolution policy in
:ref:`config.cluster.v3.Cluster.DnsLookupFamily <envoy_v3_api_enum_config.cluster.v3.Cluster.DnsLookupFamily>`
to the :ref:`ALL <envoy_v3_api_enum_value_config.cluster.v3.Cluster.DnsLookupFamily.ALL>` option to return
both IPv4 and IPv6 addresses. For clusters which use
:ref:`EDS<envoy_v3_api_enum_value_config.cluster.v3.Cluster.DiscoveryType.EDS>`, this behavior is configured
by specifying additional IP addresses for a host using the
:ref:`additional_addresses <envoy_v3_api_field_config.endpoint.v3.Endpoint.additional_addresses>` field.
The addresses specified in this field will be appended in a list to the one specified in
:ref:`address <envoy_v3_api_field_config.endpoint.v3.Endpoint.address>`

The list of all addresses will be sorted according the the Happy Eyeballs
specification and a connection will be attempted to the first in the list. If this connection succeeds,
it will be used. If it fails, an attempt will be made to the next on the list. If after 300ms the connection
is still connecting, then a backup connection attempt will be made to the next address on the list.

Eventually an attempt will succeed to one of the addresses in which case that connection will be used, or else
all attempts will fail in which case a connection error will be reported.

HTTP/3 has limited Happy-Eyeballs-like support.
When using ref:`auto_config <envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.auto_config>`
for HTTP/3 with TCP-failover, Envoy will make a best-effort attempt to try two address families. As with TCP
Happy Eyeballs support, Envoy allows 300ms for the first HTTP/3 attempt to connect. If the connection explicitly
fails or the 300ms timeout expires, if DNS resolution results in the first two resolved addresses being of
different address families, a second HTTP/3 connection pool using the second address will be created and Envoy
will attempt to establish an HTTP/3 connection using the alternate address family. In this case HTTP/3 will only
be marked broken if TCP connectivity is established and both HTTP/3 connections fail.

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
* Downstream :ref:`filter state objects <arch_overview_advanced_filter_state_sharing>` that are hashable
  and marked as shared with the upstream connection.

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
