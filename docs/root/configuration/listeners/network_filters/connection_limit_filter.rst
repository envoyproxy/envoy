.. _config_network_filters_connection_limit:

Connection Limit Filter
=======================

Background
----------

Network connections are a limited resource that we need some functionality to protect.
Envoy has the capability to limit the rate of new connections via the L4 `local rate limit filter <https://www.envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/local_rate_limit_filter>`_.
It would be useful to be able to limit the number of connections on a filter chain basis or based on some descriptors from the request.

Goals
-----

1. Protection for resources such as connections, CPU, memory, etc. by making sure every filter chain gets fair share of connection resources.
2. Preventing any single entity based on filter chain match or descriptors from consuming a large number of connections to ensure fair share of the connections.

Overview
--------

The connection limit filter applies a connection limit to incoming connections that are processed by the filter's filter chain.
Each connection processed by the filter marked as an active connection, and if the number of active connections reaches the max connections limit,
the connection will be closed without further filter iteration.

-  The connection limit filter is similar to the L4 local rate limit filter, but instead of enforcing the limit on connections rate, the filter limits the number of active connections.
-  The filter maintains an atomic counter of active connection count. It has a max connections limit value based on the configured total number of connections.
   When a new connection request comes, the filter tries to increment the connection counter. The connection is allowed if the counter is less than the max connections limit, otherwise the connection gets rejected.
   When an active connection is closed, the filter decrements the active connection counter.
-  The filter does not stop connection creation but will close the connections that were accepted but were deemed as overlimit.
-  **Slow rejection:** The filter can stop reading from the connection and close it after a delay instead of rejecting it right away or letting requests go through before the rejection.
   This way we can prevent a malicious entity from opening new connections while draining their resources.

.. note::
  In the current implementation each filter chain has an independent connection limit.

Algorithm
---------

The filter will use the reference counting algorithm to keep trace of active connection count.

1. Active connection count < max connection limit:

   -  Increment count for a new allowed connection.
   -  Decrement count when a connection closes.

2. Active connection count >= max connection limit:

   -  Close the new connection request after configured delay time.

API Reference
-------------

[extensions.filters.network.connection_limit.v3.ConnectionLimit proto]

{

**“stat_prefix”**: “…”,

**“max_connections”**: “…”,

**“delay”**: “{…}”,

**“runtime_enabled”**: “{…}”

}

**stat_prefix**

(`string <https://developers.google.com/protocol-buffers/docs/proto#scalar>`_)
The prefix to use when emitting statistics.

**max_connections**

(`uint64 <https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#google.protobuf.UInt64Value>`_)
The limit supplied in max connections.

**delay**

(`Duration <https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#duration>`_)
The delay in milliseconds for the slow rejection connections. If not set, this defaults to 0ms.

**runtime_enabled**

(`config.core.v3.RuntimeFeatureFlag <https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/core/v3/base.proto#envoy-v3-api-msg-config-core-v3-runtimefeatureflag>`_)
Runtime flag that controls whether the filter is enabled or not. If not specified, defaults to enabled.

.. _config_network_filters_connection_limit_stats:

Statistics
----------

The connection limit filter outputs statistics in the *<stat_prefix>.connection_limit* namespace.

+-----------------------+-----------------------+-----------------------+
| **Name**              | **Type**              | **Description**       |
+=======================+=======================+=======================+
| limited_connections   | Counter               | Total number of       |
|                       |                       | connections got       |
|                       |                       | rejected by this      |
|                       |                       | connection limit      |
|                       |                       | filter                |
+-----------------------+-----------------------+-----------------------+
| active_connections    | Gauge                 | Number of currently   |
|                       |                       | active connections    |
+-----------------------+-----------------------+-----------------------+
