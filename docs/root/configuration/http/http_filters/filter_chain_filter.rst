.. _config_http_filters_filter_chain:

Filter Chain
============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.filter_chain.v3.FilterChainConfig>`

The filter chain filter acts as a wrapper that applies a configurable set of HTTP filters to
incoming requests. It supports an optional ``default_filter_chain`` at the filter level and optional
per-route overrides at route or virtual host level, all merged together using name-based override
semantics.

The filter chain filter is designed as a general solution for route level filter chains. Because
the ``filter_chain`` filter itself is also an entry in the HTTP connection manager's ``http_filters``
list, it can flexibly hybrid route level filters with global ``http_filters`` and control their
relative order.

Design Goals
------------

Different services or individual routes behind the same listener often need
completely different filter chains in the gateway. Route ``A`` may need to be rate limited, while
route ``B`` may need a custom Lua script and nothing else. It is the set of filters that differs,
not only their configuration. That is something the HTTP connection manager's
``http_filters`` list cannot
express on its own: it is a single, listener-wide chain, and every filter in it becomes part of the
request processing chain for every request, regardless of which route the request matches.

:ref:`typed_per_filter_config <envoy_v3_api_field_config.route.v3.Route.typed_per_filter_config>`
does not close the gap either, because it lets a route override the configuration of a filter
without changing which filters are in the chain. Every filter that any route needs must still be
declared in ``http_filters``, so all the routes' filters end up concentrated in one list and all of
them take part in request processing. Making a filter a no-op on the routes that do not want it
while it executes its actual logic on the routes that do has to be encoded in each filter's own
per-route configuration, which is hard to control and does not work uniformly across filters.

Envoy does support a general ``disabled`` flag in both :ref:`http_filters
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.disabled>` and
:ref:`typed_per_filter_config <envoy_v3_api_field_config.route.v3.FilterConfig.disabled>` (see
:ref:`route based filter chain <arch_overview_http_filters_route_based_filter_chain>`), which
allows flexible control over which filters are added to the chain of request processing and which
are skipped. That addresses much of the problem, but it is still not perfect, because it requires
all the filters involved to support per-route configuration, because a route that needs a new
filter still forces a change to ``http_filters`` to insert an entry for it, and because different
routes cannot have different filter orders as long as the order is fixed by the single
``http_filters`` list.

The filter chain filter takes a different approach: it inserts a single entry in ``http_filters``
that acts as a placeholder, and then lets users configure a complete route level filter chain in
the route through :ref:`FilterChainConfigPerRoute
<envoy_v3_api_msg_extensions.filters.http.filter_chain.v3.FilterChainConfigPerRoute>`. Each route
decides which filters it needs and in which order without any of them appearing in
``http_filters``, so adding a filter to one route never requires touching the listener-wide list.
And with the optional ``default_filter_chain``, a default chain can be provided for all routes,
which individual routes then extend or override by name.

In most cases route level filter chains and global filters are complementary rather than
exclusive, and the filter chain filter makes it possible to hybrid them freely. Because the
placeholder is just an ordinary entry in ``http_filters``, its position decides where the route
level chain runs relative to the global filters: everything listed before it runs first and
everything listed after it runs last. Multiple ``filter_chain`` entries may also be configured in
``http_filters`` to support multiple route level chains in a single route and order them freely
against the global filters, for example one route level chain before the global authentication
filter and another one after it. This matches the more usual scenario in practice, where the
platform/admin team configures the global filters and controls their order, and the application
team then configures its own route level filter chain in the slot the platform team left for it.

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

Hybrid Route Level and Global Filters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The platform team owns the global ``http_filters`` list and pins the order: the RBAC filter always
runs first, the application's own filters run next, and the global header mutation filter always
runs last. The application team only fills in the ``filter_chain`` slot in the middle and cannot
change the surrounding order:

.. code-block:: yaml

  http_filters:
  # Global filter owned by the platform team. Always runs first.
  - name: envoy.filters.http.rbac
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
      rules:
        action: ALLOW
        policies:
          all:
            permissions:
            - any: true
            principals:
            - any: true
  # Extension point left for the application team. No default chain is configured here, so
  # routes that do not set a per-route config simply pass through.
  - name: envoy.filters.http.filter_chain
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  # Global filter owned by the platform team. Always runs after the route level chain.
  - name: envoy.filters.http.header_mutation
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
      mutations:
        request_mutations:
        - append:
            header:
              key: x-global-tag
              value: "true"
            append_action: APPEND_IF_EXISTS_OR_ADD
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

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
          - name: add-api-tag
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation
              mutations:
                request_mutations:
                - append:
                    header:
                      key: x-api-tag
                      value: "true"
                    append_action: APPEND_IF_EXISTS_OR_ADD

Requests to ``/api/`` are processed by ``rbac`` -> ``add-api-tag`` -> ``header_mutation`` ->
``router``.

If more than one extension point is needed, for example one before the RBAC filter and one after
it, add another ``filter_chain`` filter at that position in ``http_filters``. Give each instance a
distinct ``name``, because ``typed_per_filter_config`` is keyed by the configured filter name:

.. code-block:: yaml

  http_filters:
  - name: pre-auth-chain
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  - name: envoy.filters.http.rbac
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
      # ...
  - name: post-auth-chain
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.filter_chain.v3.FilterChainConfig
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

A route then targets each extension point independently by using ``pre-auth-chain`` or
``post-auth-chain`` as the ``typed_per_filter_config`` key.

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
* **Position in the global chain**: The route level filters run exactly where the
  ``filter_chain`` filter sits in the ``http_filters`` list. Routes cannot move that position,
  so the global ordering owned by the platform team is preserved.
* **Route match timing**: Only the initial route match determines which per-route chains are
  collected. Subsequent internal route refreshes do not change the active chains.
* **Override change order**: If a filter X is overridden by name in a more-specific chain,
  the less-specific X is skipped entirely and the most-specific one will run. But note
  that the order of the filters in the final chain is always from least to most specific, so the
  less-specific filters run first and the X from the more-specific chain runs
  after previous filters. This changes the order of execution for X. If the order matters,
  consider to override the whole chain.
