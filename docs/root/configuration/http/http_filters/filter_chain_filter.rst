.. _config_http_filters_filter_chain:

Filter Chain
============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.filter_chain.v3.FilterChainConfig>`

The filter chain filter acts as a wrapper that applies a configurable chain of HTTP filters
to incoming requests. This allows you to define reusable filter chains that can be applied
selectively based on route configuration.

The filter supports:

* A default filter chain that is applied when no route-specific configuration is present.
* Named filter chains that can be referenced by route-level configuration.
* Inline filter chains defined directly in per-route configuration.

The filter processes the request through the configured filter chain in order during the
decode phase, and in reverse order during the encode phase.

Configuration
-------------

Filter-level Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~

The filter-level configuration (:ref:`FilterChainConfig <envoy_v3_api_msg_extensions.filters.http.filter_chain.v3.FilterChainConfig>`)
defines:

* ``filter_chain``: The default filter chain to apply when no route-specific configuration matches.

Per-Route Configuration
~~~~~~~~~~~~~~~~~~~~~~~

The per-route configuration (:ref:`FilterChainConfigPerRoute <envoy_v3_api_msg_extensions.filters.http.filter_chain.v3.FilterChainConfigPerRoute>`)
allows different filter chains to be applied on different routes:

* ``filter_chain``: An inline filter chain definition that takes precedence if specified.

Example Configuration
---------------------

Basic Configuration
~~~~~~~~~~~~~~~~~~~

This example configures a default filter chain with a buffer filter:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.filter_chain
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
      filter_chain:
        filters:
        - name: envoy.filters.http.buffer
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
            max_request_bytes: 65536
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

Inline Per-Route Filter Chain
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can also define a filter chain inline in the per-route configuration:

.. code-block:: yaml

  routes:
  - match:
      prefix: "/upload/"
    route:
      cluster: upload_cluster
    typed_per_filter_config:
      envoy.filters.http.filter_chain:
        "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfigPerRoute
        filter_chain:
          filters:
          - name: envoy.filters.http.buffer
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
              max_request_bytes: 104857600

Behavior Notes
--------------

* When no filter chain is configured (either at filter level or route level), the filter
  passes through all requests without modification.
* The filter chain is resolved once per request during the initialization of the filter chain.
* Filters in the chain are executed in order during decoding (request processing) and
  in reverse order during encoding (response processing).
