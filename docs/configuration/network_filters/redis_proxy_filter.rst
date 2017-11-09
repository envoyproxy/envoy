.. _config_network_filters_redis_proxy:

Redis proxy
===========

Redis :ref:`architecture overview <arch_overview_redis>`.

.. code-block:: json

  {
    "name": "redis_proxy",
    "config": {
      "cluster_name": "...",
      "conn_pool": "{...}",
      "stat_prefix": "..."
    }
  }

cluster_name
  *(required, string)* Name of cluster from cluster manager.
  See the :ref:`configuration section <arch_overview_redis_configuration>` of the architecture
  overview for recommendations on configuring the backing cluster.

conn_pool
  *(required, object)* Connection pool configuration.

stat_prefix
  *(required, string)* The prefix to use when emitting :ref:`statistics
  <config_network_filters_redis_proxy_stats>`.

Connection pool configuration
-----------------------------

.. code-block:: json

  {
    "op_timeout_ms": "...",
  }

op_timeout_ms
  *(required, integer)* Per-operation timeout in milliseconds. The timer starts when the first
  command of a pipeline is written to the backend connection. Each response received from Redis
  resets the timer since it signifies that the next command is being processed by the backend.
  The only exception to this behavior is when a connection to a backend is not yet established. In
  that case, the connect timeout on the cluster will govern the timeout until the connection is
  ready.

.. _config_network_filters_redis_proxy_stats:

Statistics
----------

Every configured Redis proxy filter has statistics rooted at *redis.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  downstream_cx_active, Gauge, Total active connections
  downstream_cx_protocol_error, Counter, Total protocol errors
  downstream_cx_rx_bytes_buffered, Gauge, Total received bytes currently buffered
  downstream_cx_rx_bytes_total, Counter, Total bytes received
  downstream_cx_total, Counter, Total connections
  downstream_cx_tx_bytes_buffered, Gauge, Total sent bytes currently buffered
  downstream_cx_tx_bytes_total, Counter, Total bytes sent
  downstream_cx_drain_close, Counter, Number of connections closed due to draining
  downstream_rq_active, Gauge, Total active requests
  downstream_rq_total, Counter, Total requests


Splitter statistics
-------------------

The Redis filter will gather statistics for the command splitter in the
*redis.<stat_prefix>.splitter.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  invalid_request, Counter, "Number of requests with an incorrect number of arguments"
  unsupported_command, Counter, "Number of commands issued which are not recognized by the
  command splitter"

Per command statistics
----------------------

The Redis filter will gather statistics for commands in the
*redis.<stat_prefix>.command.<command>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total, Counter, Number of commands

.. _config_network_filters_redis_proxy_per_command_stats:

Runtime
-------

The Redis proxy filter supports the following runtime settings:

redis.drain_close_enabled
  % of connections that will be drain closed if the server is draining and would otherwise
  attempt a drain close. Defaults to 100.
