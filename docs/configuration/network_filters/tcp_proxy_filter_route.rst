.. _config_network_filters_tcp_proxy_route:

Route
=====

A TCP proxy route consists of a set of optional L4 criteria and the name of a :ref:`cluster manager <arch_overview_cluster_manager>` to use for a connection if it mateches all the specified criteria. Routes are tried in the order specified until a match is found. If no match is found, the connection is closed. A route with no criteria is valid and always produces a match.

.. code-block:: json

  {
    "cluster": "...",
    "destination_ip_list": [],
    "destination_ports": "...",
    "source_ip_list": [],
    "source_ports": "..."
  }

cluster
  *(required, string)* The :ref:`cluster manager <arch_overview_cluster_manager>` cluster to connect
  to when a the downstream network connection matches the specified criteria.

destination_ip_list
  *(optional, array)*  An optional list of IPv4 subnets in the form "a.b.c.d/xx".
  The criteria is satisfied if the destination IP address of the downstream connection is contained in at least one of the specified subnets.
  If the parameter is not specified or the list is empty, the destination IP address is ignored.
  The destination IP address of the downstream connection might be different from the addresses on which the proxy is listening if the connection has been redirected using iptables.
  Example:

 .. code-block:: json

    [
      "192.168.3.0/24",
      "50.1.2.3/32",
      "10.15.0.0/16"
    ]

destination_ports
  *(optional, string)* An optional string containing a comma-separated list of port numbers or ranges.
  The criteria is satisfied if the destination port of the downstream connection is contained in at least one of the specified ranges.
  If the parameter is not specified or the list is empty, the destination port is ignored.
  The destination port address of the downstream connection might be different from the port on which the proxy is listening if the connection has been redirected using iptables.
  Example:

 .. code-block:: json

  {
    "destination_ports": "1-1024,2048-4096,12345"
  }

source_ip_list
  *(optional, array)*  An optional list of IPv4 subnets in the form "a.b.c.d/xx".
  The criteria is satisfied if the source IP address of the downstream connection is contained in at least one of the specified subnets.
  If the parameter is not specified or the list is empty, the source IP address is ignored.
  Example:

 .. code-block:: json

    [
      "192.168.3.0/24",
      "50.1.2.3/32",
      "10.15.0.0/16"
    ]

source_ports
  *(optional, string)* An optional string containing a comma-separated list of port numbers or ranges.
  The criteria is satisfied if the source port of the downstream connection is contained in at least one of the specified ranges.
  If the parameter is not specified or the list is empty, the source port is ignored.
  Example:

 .. code-block:: json

  {
    "source_ports": "1-1024,2048-4096,12345"
  }
