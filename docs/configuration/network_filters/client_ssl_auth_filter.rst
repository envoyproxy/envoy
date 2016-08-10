.. _config_network_filters_client_ssl_auth:

Client SSL authentication
=========================

Client SSL authentication filter :ref:`architecture overview <arch_overview_ssl_auth_filter>`.

.. code-block:: json

  {
    "type": "read",
    "name": "client_ssl_auth",
    "config": {
      "auth_api_cluster": "...",
      "stat_prefix": "...",
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

ip_white_list
  *(optional, array)* An optional list of IP address and subnet masks that should be white listed
  for access by the filter. If no list is provided, there is no IP white list. The list is
  specified as in the following example:

  .. code-block:: json

    [
      "192.168.3.0/24",
      "50.1.2.3/32",
      "10.15.0.0/16"
    ]

.. _config_network_filters_client_ssl_auth_stats:

Statistics
----------

Every configured client SSL authentication filter has statistics rooted at
*auth.clientssl.<stat_prefix>.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  update_success, Counter, Description
  update_failure, Counter, Description
  auth_no_ssl, Counter, Description
  auth_ip_white_list, Counter, Description
  auth_digest_match, Counter, Description
  auth_digest_no_match, Counter, Description
  total_principals, Gauge, Description

Runtime
-------

The client SSL authentication filter supports the following runtime settings:

auth.clientssl.refresh_interval_ms
  FIXFIX

.. _config_network_filters_client_ssl_auth_rest_api:

REST API
--------

FIXFIX
