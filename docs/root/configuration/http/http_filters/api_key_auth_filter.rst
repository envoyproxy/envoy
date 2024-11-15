.. _config_http_filters_api_key_auth:

API key auth
============

This HTTP filter can be used to authenticate users based on the unique API key. The filter will
extract the API keys from either an HTTP header, a parameter query, or a cookie and verify them against
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

.. literalinclude:: _include/api-key-auth-filter.yaml
    :language: yaml
    :lines: 46-56
    :linenos:
    :caption: :download:`api-key-auth-filter.yaml <_include/api-key-auth-filter.yaml>`

Per-Route Configuration
-----------------------

It's possible to override the filter's configuration for a specific scope like a route or virtual host.
And the overriding configuration could be partial to override only credential list or to override only
the API key source.

And this filter also provides very limited authorization control. A simple ``allowed_clients`` could be
configured for specific scope like a route or virtual host to allow or deny specific clients.

An example scope specific configuration of the filter may look like the following:

.. literalinclude:: _include/api-key-auth-filter.yaml
    :language: yaml
    :lines: 16-45
    :linenos:
    :caption: :download:`api-key-auth-filter.yaml <_include/api-key-auth-filter.yaml>`

In this example we customize credential list and key source for ``/admin`` route, and disable
authentication for ``/static`` prefixed routes.

Combining the per-route configuration example and the filter configuration example, given the following
requests, the filter will behave as follows:

.. code-block:: text

  # The request will be allowed because the API key is valid and the client is allowed.
  GET /admin?api_key=another_key HTTP/1.1
  host: example.com

  # The request will be denied with 403 status code because the API key is valid but the client is
  # not allowed.
  GET /admin?api_key=one_key HTTP/1.1
  host: example.com

  # The request will be denied with 401 status code because the API key is invalid.
  GET /admin?api_key=invalid_key HTTP/1.1
  host: example.com

  # The request will be allowed because the filter is disabled for specific route.
  GET /static HTTP/1.1
  host: example.com

  # The request will be allowed because the API key is valid and no client validation is configured.
  GET / HTTP/1.1
  host: example.com
  Authorization: "Bearer one_key"

Statistics
----------

The HTTP API key auth filter outputs statistics in the ``http.<stat_prefix>.api_key_auth.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  allowed, Counter, Total number of allowed requests
  unauthorized, Counter, Total number of requests that have invalid API key
  forbidden, Counter, Total number of requests that have valid API key but not allowed
