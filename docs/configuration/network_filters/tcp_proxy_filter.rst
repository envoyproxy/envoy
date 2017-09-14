.. _config_network_filters_tcp_proxy:

TCP proxy
=========

TCP proxy :ref:`architecture overview <arch_overview_tcp_proxy>`.

.. code-block:: json

  {
    "name": "tcp_proxy",
    "config": {
      "stat_prefix": "...",
      "route_config": "{...}"
    }
  }

:ref:`route_config <config_network_filters_tcp_proxy_route_config>`
  *(required, object)* The route table for the filter.
  All filter instances must have a route table, even if it is empty.

stat_prefix
  *(required, string)* The prefix to use when emitting :ref:`statistics
  <config_network_filters_tcp_proxy_stats>`.

.. _config_network_filters_tcp_proxy_route_config:

Route Configuration
-------------------

.. code-block:: json

  {
    "routes": []
  }

:ref:`routes <config_network_filters_tcp_proxy_route>`
  *(required, array)* An array of route entries that make up the route table.

.. _config_network_filters_tcp_proxy_route:

Route
-----

A TCP proxy route consists of a set of optional L4 criteria and the name of a
:ref:`cluster <config_cluster_manager_cluster>`. If a downstream connection matches
all the specified criteria, the cluster in the route is used for the corresponding upstream
connection. Routes are tried in the order specified until a match is found. If no match is
found, the connection is closed. A route with no criteria is valid and always produces a match.

.. code-block:: json

  {
    "cluster": "...",
    "destination_ip_list": [],
    "destination_ports": "...",
    "source_ip_list": [],
    "source_ports": "..."
  }

cluster
  *(required, string)* The :ref:`cluster <config_cluster_manager_cluster>` to connect
  to when a the downstream network connection matches the specified criteria.

destination_ip_list
  *(optional, array)*  An optional list of IP address subnets in the form "ip_address/xx".
  The criteria is satisfied if the destination IP address of the downstream connection is
  contained in at least one of the specified subnets.
  If the parameter is not specified or the list is empty, the destination IP address is ignored.
  The destination IP address of the downstream connection might be different from the addresses
  on which the proxy is listening if the connection has been redirected.  Example:

 .. code-block:: json

    [
      "192.168.3.0/24",
      "50.1.2.3/32",
      "10.15.0.0/16",
      "2001:abcd::/64"
    ]

destination_ports
  *(optional, string)* An optional string containing a comma-separated list of port numbers or
  ranges. The criteria is satisfied if the destination port of the downstream connection
  is contained in at least one of the specified ranges.
  If the parameter is not specified, the destination port is ignored. The destination port address
  of the downstream connection might be different from the port on which the proxy is listening if
  the connection has been redirected. Example:

 .. code-block:: json

  {
    "destination_ports": "1-1024,2048-4096,12345"
  }

source_ip_list
  *(optional, array)*  An optional list of IP address subnets in the form "ip_address/xx".
  The criteria is satisfied if the source IP address of the downstream connection is contained
  in at least one of the specified subnets. If the parameter is not specified or the list is empty,
  the source IP address is ignored. Example:

 .. code-block:: json

    [
      "192.168.3.0/24",
      "50.1.2.3/32",
      "10.15.0.0/16",
      "2001:abcd::/64"
    ]

source_ports
  *(optional, string)* An optional string containing a comma-separated list of port numbers or
  ranges. The criteria is satisfied if the source port of the downstream connection is contained
  in at least one of the specified ranges. If the parameter is not specified, the source port is
  ignored.  Example:

 .. code-block:: json

  {
    "source_ports": "1-1024,2048-4096,12345"
  }

.. _config_network_filters_tcp_proxy_stats:

Statistics
----------

The TCP proxy filter emits both its own downstream statistics as well as many of the :ref:`cluster
upstream statistics <config_cluster_manager_cluster_stats>` where applicable. The downstream
statistics are rooted at *tcp.<stat_prefix>.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  downstream_cx_total, Counter, Total number of connections handled by the filter.
  downstream_cx_no_route, Counter, Number of connections for which no matching route was found.
  downstream_cx_tx_bytes_total, Counter, Total bytes written to the downstream connection.
  downstream_cx_tx_bytes_buffered, Gauge, Total bytes currently buffered to the downstream connection.
  downstream_flow_control_paused_reading_total, Counter, Total number of times flow control paused reading from downstream.
  downstream_flow_control_resumed_reading_total, Counter, Total number of times flow control resumed reading from downstream.
