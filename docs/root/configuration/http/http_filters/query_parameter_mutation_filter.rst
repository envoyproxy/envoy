.. _config_http_filters_query_parameter_mutation:

Query Parameter Mutation
========================

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.query_parameter_mutation.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.query_parameter_mutation.v3.Config>`

This filter can be used to add or remove query parameters from a request. Removals are applied to the request before additions. Per-route
configurations are supported with this filter.

Example configurations
----------------------

The following configuration will transform requests from `/some/path?remove-me=value` into `/some/path?param=new-value`.

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.query_parameter_mutation
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.query_parameter_mutation.v3.Config
        query_parameters_to_add:
          - key: param
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
              envoy.filters.http.query_parameter_mutation:
                "@type": type.googleapis.com/envoy.extensions.filters.http.query_parameter_mutation.v3.Config
                query_parameters_to_add:
                  - key: param
                    value: new-value
          - match: { path: / }
            route: { cluster: service2 }

  http_filters:
    - name: envoy.filters.http.query_parameter_mutation
    - name: envoy.filters.http.router
