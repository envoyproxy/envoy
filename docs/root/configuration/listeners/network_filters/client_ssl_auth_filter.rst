.. _config_network_filters_client_ssl_auth:

Client TLS authentication
=========================

* Client TLS authentication filter :ref:`architecture overview <arch_overview_ssl_auth_filter>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.client_ssl_auth.v3.ClientSSLAuth``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.client_ssl_auth.v3.ClientSSLAuth>`

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
  auth_ip_allowlist, Counter, Total connections allowed due to the IP allowlist
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
