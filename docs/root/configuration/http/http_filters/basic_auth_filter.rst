.. _config_http_filters_basic_auth:

Basic Auth
==========

This HTTP filter can be used to authenticate user credentials in the HTTP Authentication header defined
in `RFC7617 <https://tools.ietf.org/html/rfc7617>`.

The filter will extract the username and password from the HTTP Authentication header and verify them
against the configured username and password list.

If the username and password are valid, the request will be forwared to the next filter in the filter chains.
If they're invalid or not provided in the HTTP request, the request will be denied with a 401 Unauthorized response.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.basic_auth.v3.BasicAuth``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.basic_auth.v3.BasicAuth>`

``users`` is a list of username-password pairs used to verify user credentials in the "Authorization" header.
 The value needs to be the `htpasswd <https://httpd.apache.org/docs/2.4/programs/htpasswd.html>` format.


An example configuration of the filter may look like the following:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.basic_auth
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.basic_auth.v3.BasicAuth
      users:
        inline_string: |-
          user1:{SHA}hashed_user1_password
          user2:{SHA}hashed_user2_password

Note that only SHA format is currently supported. Other formats may be added in the future.

Per-Route Configuration
-----------------------

An example configuration of the route filter may look like the following:

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
          envoy.filters.http.basic_auth:
            "@type": type.googleapis.com/envoy.extensions.filters.http.basic_auth.v3.BasicAuthPerRoute
            users:
              inline_string: |-
                admin:{SHA}hashed_admin_password
      - match: { prefix: "/static" }
        route: { cluster: some_service }
        typed_per_filter_config:
          envoy.filters.http.basic_auth:
            "@type": type.googleapis.com/envoy.config.route.v3.FilterConfig
            disabled: true
      - match: { prefix: "/" }
        route: { cluster: some_service }

In this example we customize users for ``/admin`` route, and disable authentication for ``/static`` prefixed routes.

Statistics
----------

The HTTP basic auth filter outputs statistics in the ``http.<stat_prefix>.basic_auth.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  allowed, Counter, Total number of allowed requests
  denied, Counter, Total number of denied requests
