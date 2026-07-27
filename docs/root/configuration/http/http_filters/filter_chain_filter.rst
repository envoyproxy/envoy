.. _config_http_filters_filter_chain:

Filter Chain
============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.filter_chain.v3.FilterChainConfig>`

The filter chain filter acts as a wrapper that applies a configurable set of HTTP filters to
incoming requests. It supports a ``default_filter_chain`` at the filter level and optional
per-route overrides, all merged together using name-based override semantics.

Overview
--------

When a request arrives the filter collects all active filter chains in order from least to
most specific:

1. ``default_filter_chain`` (from the filter-level ``FilterChainConfig``)
2. Per-route chains from outermost to innermost scope (e.g. virtual-host level, then route level)

Each filter in a less-specific chain is applied **unless** a more-specific chain contains a
filter with the same ``name``. In that case the less-specific entry is silently skipped.

This lets per-route configuration selectively extend or replace the default chain without
redefining it entirely.

If no chains are resolved for a request the filter passes through without any modification and
the ``pass_through`` counter is incremented.

Configuration
-------------

Filter-level Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~

The filter-level configuration (:ref:`FilterChainConfig <envoy_v3_api_msg_extensions.filters.http.filter_chain.v3.FilterChainConfig>`)
accepts one optional field:

* ``default_filter_chain``: The default chain applied to every request. Can be overridden per
  route using ``FilterChainConfigPerRoute``.

Per-Route Configuration
~~~~~~~~~~~~~~~~~~~~~~~

The per-route configuration (:ref:`FilterChainConfigPerRoute <envoy_v3_api_msg_extensions.filters.http.filter_chain.v3.FilterChainConfigPerRoute>`)
accepts one required field:

* ``filter_chain``: An inline filter chain. Filters in this chain override same-named filters
  from less-specific chains (e.g. the default chain).

Statistics
----------

The filter emits the following counters under the ``<stat_prefix>filter_chain.`` namespace:

.. csv-table::
   :header: Name, Type, Description
   :widths: auto

   pass_through, Counter, Number of requests for which no filter chain was resolved (the filter passed through without modification)

Example Configurations
----------------------

Basic Default Chain
~~~~~~~~~~~~~~~~~~~

Applies a header-mutation filter to every request by default:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.filter_chain
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
      default_filter_chain:
        filters:
        - name: envoy.filters.http.buffer
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
            mutations:
              request_mutations:
              - append:
                  header:
                    key: x-default-tag
                    value: "true"
                  append_action: APPEND_IF_EXISTS_OR_ADD
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

Per-Route Filter Chain
~~~~~~~~~~~~~~~~~~~~~~

The default chain adds ``x-default-tag`` to every request. The ``/upload/`` route adds its
own ``x-upload-tag`` header. Both filters run because their names are distinct — the
per-route chain extends the default rather than replacing it:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.filter_chain
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
      default_filter_chain:
        filters:
        - name: envoy.filters.http.header_mutation
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
            mutations:
              request_mutations:
              - append:
                  header:
                    key: x-default-tag
                    value: "true"
                  append_action: APPEND_IF_EXISTS_OR_ADD

  routes:
  - match:
      prefix: /upload/
    route:
      cluster: upload_cluster
    typed_per_filter_config:
      envoy.filters.http.filter_chain:
        "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfigPerRoute
        filter_chain:
          filters:
          - name: add-upload-tag                   # distinct name — does NOT override the default
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
              mutations:
                request_mutations:
                - append:
                    header:
                      key: x-upload-tag
                      value: "true"
                    append_action: APPEND_IF_EXISTS_OR_ADD

Overriding a Default Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The default chain and the per-route chain both configure a filter named
``envoy.filters.http.header_mutation``. Because the names match, the per-route definition
wins — only the per-route version of that filter runs on the ``/api/`` route. The default
version is skipped entirely:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.filter_chain
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
      default_filter_chain:
        filters:
        - name: envoy.filters.http.header_mutation
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
            mutations:
              request_mutations:
              - append:
                  header:
                    key: x-tag
                    value: default
                  append_action: APPEND_IF_EXISTS_OR_ADD

  routes:
  - match:
      prefix: /api/
    route:
      cluster: api_cluster
    typed_per_filter_config:
      envoy.filters.http.filter_chain:
        "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfigPerRoute
        filter_chain:
          filters:
          - name: envoy.filters.http.header_mutation  # same name — overrides the default
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
              mutations:
                request_mutations:
                - append:
                    header:
                      key: x-tag
                      value: api-specific
                    append_action: APPEND_IF_EXISTS_OR_ADD

Behavior Notes
--------------

* **No chains resolved**: If no filter chain exists at either the filter level or the route
  level the filter passes through without modification and increments ``pass_through``.
* **Merge order**: The default chain always runs first (least specific). Per-route chains run
  after in scope order from outermost (virtual-host) to innermost (route). Within each chain
  filters are applied in the order they are listed.
* **Override by name**: A filter in a less-specific chain is skipped if any more-specific chain
  defines a filter with the same ``name`` field. The ``name`` field is the sole key for
  override resolution — the typed config type does not matter.
* **Route match timing**: Only the initial route match determines which per-route chains are
  collected. Subsequent internal route refreshes do not change the active chains.
* **Override change order**: If a filter X is overridden by name in a more-specific chain,
  the less-specific X is skipped entirely and the most-specific one will run. But note
  that the order of the filters in the final chain is always from least to most specific, so the
  less-specific filters run first and the X from the more-specific chain runs
  after previous filters. This changes the order of execution for X. If the order matters,
  consider to override the whole chain.
