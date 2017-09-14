.. _config_network_filters_client_ssl_auth:

Client TLS authentication
=========================

Client TLS authentication filter :ref:`architecture overview <arch_overview_ssl_auth_filter>`.

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

.. _config_network_filters_client_ssl_auth_stats:

Statistics
----------

Every configured client TLS authentication filter has statistics rooted at
*auth.clientssl.<stat_prefix>.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  update_success, Counter, Total principal update successes
  update_failure, Counter, Total principal update failures
  auth_no_ssl, Counter, Total connections ignored due to no TLS
  auth_ip_white_list, Counter, Total connections allowed due to the IP white list
  auth_digest_match, Counter, Total connections allowed due to certificate match
  auth_digest_no_match, Counter, Total connections denied due to no certificate match
  total_principals, Gauge, Total loaded principals

.. _config_network_filters_client_ssl_auth_rest_api:

REST API
--------

.. http:get:: /v1/certs/list/approved

  The authentication filter will call this API every refresh interval to fetch the current list
  of approved certificates/principals. The expected JSON response looks like:

  .. code-block:: json

    {
      "certificates": []
    }

  certificates
    *(required, array)* list of approved certificates/principals.

  Each certificate object is defined as:

  .. code-block:: json

    {
      "fingerprint_sha256": "...",
    }

  fingerprint_sha256
    *(required, string)* The SHA256 hash of the approved client certificate. Envoy will match this
    hash to the presented client certificate to determine whether there is a digest match.
