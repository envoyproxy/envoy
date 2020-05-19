
.. _config_http_filters_oauth:

OAuth
=====

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.oauth.v3.OAuth2>`
* This filter should be configured with the name *envoy.filters.http.oauth*.

.. attention::

  The OAuth filter is currently under active development.

Example configuration
---------------------

.. code-block:: yaml
   http_filters:
   - name: oauth2
     config:
       credentials:
         client_id: a
         client_secret: b
         token_secret: c
       secrets_cluster: local_sds
       cluster: auth
       hostname: auth.example.com
   - name: envoy.router

  clusters:
  - name: service
    ...
  - name: auth
    connect_timeout: 5s
    type: LOGICAL_DNS
    lb_policy: ROUND_ROBIN
    hosts: [{ socket_address: { address: auth.example.com, port_value: 443 }}]
    tls_context: { sni: auth.example.com }

```

There is some duplicate configuration above - the oauth server hostname is defined twice, once in config and once in
a cluster. This is due to some limitations to Envoy's internal Async HTTP client.

Notes
-----

This module does not currently provide much Cross-Site-Request-Forgery protection for the redirect loop
to the OAuth server and back.

The service must be served over HTTPS for this filter to work, as the cookies use `;secure`.

Statistics
----------

The OAuth filter outputs statistics in the *http.oauth.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  failure, Counter, Total requests that were denied.
  success, Counter, Total requests that were allowed.
