.. _config_network_filters_client_ssl_auth_v1:

Client TLS authentication
=========================

Client TLS authentication :ref:`configuration overview <config_network_filters_client_ssl_auth>`.

.. code-block:: json

  {
    "name": "client_ssl_auth",
    "config": {
      "auth_api_cluster": "...",
      "stat_prefix": "...",
      "refresh_delay_ms": "...",
      "ip_white_list": []
    }
  }

auth_api_cluster
  *(required, string)* The :ref:`cluster manager <arch_overview_cluster_manager>` cluster that runs
  the authentication service. The filter will connect to the service every 60s to fetch the list
  of principals. The service must support the expected :ref:`REST API
  <config_network_filters_client_ssl_auth_rest_api>`.

stat_prefix
  *(required, string)* The prefix to use when emitting :ref:`statistics
  <config_network_filters_client_ssl_auth_stats>`.

refresh_delay_ms
  *(optional, integer)* Time in milliseconds between principal refreshes from the authentication
  service. Default is 60000 (60s). The actual fetch time will be this value plus a random jittered
  value between 0-refresh_delay_ms milliseconds.

ip_white_list
  *(optional, array)* An optional list of IP address and subnet masks that should be white listed
  for access by the filter. If no list is provided, there is no IP white list. The list is
  specified as in the following example:

  .. code-block:: json

    [
      "192.168.3.0/24",
      "50.1.2.3/32",
      "10.15.0.0/16",
      "2001:abcd::/64"
    ]
