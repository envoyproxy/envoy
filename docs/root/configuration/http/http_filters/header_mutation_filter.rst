.. _config_http_filters_header_mutation:

Header Mutation
===============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.header_mutation.v3.HeaderMutation>`

This is a filter that can be used to add, remove, append, or update HTTP headers and query parameters. It can be added in any position in the filter chain
and used as downstream or upstream HTTP filter. The filter can be configured to apply the header mutations to the request, response, or both.


In most cases, this filter would be a more flexible alternative to the ``request_headers_to_add``, ``request_headers_to_remove``,
``response_headers_to_add``, and ``response_headers_to_remove`` fields in the :ref:`route configuration <envoy_v3_api_msg_config.route.v3.RouteConfiguration>`.


The filter provides complete control over the position and order of the header mutations. It may be used to influence later route picks if
the route cache is cleared by a filter executing after the header mutation filter.


In addition, this filter can be used as upstream HTTP filter and mutate the request headers after load balancing and host selection.


Please note that as an encoder filter, this filter follows the standard rules of when it will execute in situations such as local replies - response
headers will not be unconditionally added in cases where the filter would be bypassed.


Example configurations
----------------------

The following configuration will transform requests from `/some/path?remove-me=value` into `/some/path?param=new-value`.

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.header_mutation
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
        query_parameters_to_add:
          - append_action: APPEND_IF_EXISTS_OR_ADD
            query_parameter:
              key: param
              value: new-value
        query_parameters_to_remove:
          - remove-me

    - name: envoy.filters.http.router

The following configuration will add a query parameter only on requests that match `/foobar`.

.. code-block:: yaml

  route_config:
    virtual_hosts:
      - name: service
        domains: ["*"]
        routes:
          - match: { path: /foobar }
            route: { cluster: service1 }
            typed_per_filter_config:
              envoy.filters.http.header_mutation:
                "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
                query_parameters_to_add:
                  - append_action: APPEND_IF_EXISTS_OR_ADD
                    query_parameter:
                      key: param
                      value: new-value
          - match: { path: / }
            route: { cluster: service2 }

  http_filters:
    - name: envoy.filters.http.header_mutation
    - name: envoy.filters.http.router

