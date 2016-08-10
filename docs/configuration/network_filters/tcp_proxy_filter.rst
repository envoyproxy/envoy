.. _config_network_filters_tcp_proxy:

TCP proxy
=========

TCP proxy :ref:`architecture overview <arch_overview_tcp_proxy>`.

.. code-block:: json

  {
    "type": "read",
    "name": "tcp_proxy",
    "config": {
      "cluster": "...",
      "stat_prefix": "..."
    }
  }

cluster
  *(required, string)* The :ref:`cluster manager <arch_overview_cluster_manager>` cluster to connect
  to when a new downstream network connection is received.

stat_prefix
  *(required, string)* The prefix to use when emitting :ref:`statistics
  <config_network_filters_tcp_proxy_stats>`.

.. _config_network_filters_tcp_proxy_stats:

Statistics
----------

The TCP proxy filter emits both its own downstream statistics as well as many of the :ref:`cluster
upstream statistics <config_cluster_manager_cluster_stats>` where applicable. The downstream
statistics are rooted at *"tcp.<stat_prefix>."* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  downstream_cx_tx_bytes_total, Counter, Description
  downstream_cx_tx_bytes_buffered, Gauge, Description
