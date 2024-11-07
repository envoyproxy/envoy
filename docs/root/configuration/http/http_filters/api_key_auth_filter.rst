.. _config_http_filters_api_key_auth:

API key auth
============

This HTTP filter can be used to authenticate users based on the unique API key. The filter will
extract the API keys from the HTTP header, parameter query, or cookie and verify them against
the configured credential list.

If the API key is valid and the related client is allowed, the request will be allowed to continue.
If the API key is invalid or not exists, the request will be denied with 401 status code.
If the API key is valid but the related client is not allowed, the request will be denied with
403 status code.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.api_key_auth.v3.ApiKeyAuth``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.api_key_auth.v3.ApiKeyAuth>`

An example configuration of the filter may look like the following:

.. code-block:: yaml

  http_filters:
  - name: api_key_auth
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.api_key_auth.v3.ApiKeyAuth
      credentials:
        entries:
        - api_key: one_key
          client_id: one_client
      authentication_header: Authorization

Per-Route Configuration
-----------------------

It's possible to override the filter's configuration for a specific scope like a route or virtual host.
And the overriding configuration could be partial to override only credential list or to override only
the API key source.

And this filter also provides very limited authorization control. A simple ``allowed_clients`` could be
configured for specific scope like a route or virtual host to allow or deny specific clients.

An example scope specific configuration of the filter may look like the following:

.. code-block:: yaml

  route_config:
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match: { path: "/admin" }
        route: { cluster: some_service }
        typed_per_filter_config:
          api_key_auth:
            "@type": type.googleapis.com/envoy.extensions.filters.http.api_key_auth.v3.ApiKeyAuthPerScope
            override_config:
              credentials:
                entries:
                - api_key: another_key
                  client_id: another_client
              authentication_query: api_key
            allowed_clients:
              - client_id: another_client
      - match: { prefix: "/static" }
        route: { cluster: some_service }
        typed_per_filter_config:
          api_key_auth:
            "@type": type.googleapis.com/envoy.config.route.v3.FilterConfig
            disabled: true
      - match: { prefix: "/" }
        route: { cluster: some_service }

In this example we customize credential list and key source for ``/admin`` route, and disable
authentication for ``/static`` prefixed routes.

Statistics
----------

The HTTP basic auth filter outputs statistics in the ``http.<stat_prefix>.api_key_auth.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  allowed, Counter, Total number of allowed requests
  unauthorized, Counter, Total number of requests that have not valid API key
  forbidden, Counter, Total number of requests that have valid API key but not allowed
