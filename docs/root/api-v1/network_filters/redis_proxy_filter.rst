.. _config_network_filters_redis_proxy_v1:

Redis proxy
===========

Redis proxy :ref:`configuration overview <config_network_filters_redis_proxy>`.

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
