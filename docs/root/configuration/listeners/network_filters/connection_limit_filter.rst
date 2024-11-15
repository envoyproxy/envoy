.. _config_network_filters_connection_limit:

Connection Limit Filter
=======================

* Connection limiting :ref:`architecture overview <arch_overview_connection_limit>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.connection_limit.v3.ConnectionLimit``.
* :ref:`v3 API reference
  <envoy_v3_api_msg_extensions.filters.network.connection_limit.v3.ConnectionLimit>`

Overview
--------

The filter can protect for resources such as connections, CPU, memory, etc. by making sure every filter chain
gets fair share of connection resources and prevent any single entity based on filter chain match or descriptors
from consuming a large number of connections.
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

.. _config_network_filters_connection_limit_stats:

Statistics
----------

Every configured connection limit filter has statistics rooted at *connection_limit.<stat_prefix>.*
with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  limited_connections, Counter, Total connections that have been rejected due to connection limit exceeded
  active_connections, Gauge, Number of currently active connections in the scope of this network filter chain

Runtime
-------

The connection limit filter can be runtime feature flagged via the :ref:`enabled
<envoy_v3_api_field_extensions.filters.network.connection_limit.v3.ConnectionLimit.runtime_enabled>`
configuration field.
