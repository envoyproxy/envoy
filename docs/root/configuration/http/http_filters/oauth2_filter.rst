
.. _config_http_filters_oauth:

OAuth2
======

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.oauth2.v3.OAuth2>`
* This filter should be configured with the name *envoy.filters.http.oauth2*.

.. attention::

  The OAuth2 filter is currently under active development.

Example configuration
---------------------

.. code-block::

   http_filters:
   - name: oauth2
     typed_config:
       "@type": type.googleapis.com/envoy.extensions.filters.http.oauth2.v3.OAuth2
       cluster: oauth
      hostname: oauth.com
      callback_path: /callback
      signout_path: /signout
      token_path: /token
      credentials:
        client_id: foo
        token_secret:
          name: token
        hmac_secret:
          name: hmac
      timeout: 3s
   - name: envoy.router

  clusters:
  - name: service
    ...
  - name: auth
    connect_timeout: 5s
    type: LOGICAL_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: auth
      endpoints:
      - lb_endpoints:
        - endpoint:
            address: { socket_address: { address: auth.example.com, port_value: 443 }}
    tls_context: { sni: auth.example.com }

Notes
-----

This module does not currently provide much Cross-Site-Request-Forgery protection for the redirect loop
to the OAuth server and back.

The service must be served over HTTPS for this filter to work, as the cookies use `;secure`.

Statistics
----------

The OAuth filter outputs statistics in the *<stat_prefix>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  oauth_failure, Counter, Total requests that were denied.
  oauth_success, Counter, Total requests that were allowed.
  oauth_unauthenticated_rq, Counter, Total unauthenticated requests that were redirected for authentication.
