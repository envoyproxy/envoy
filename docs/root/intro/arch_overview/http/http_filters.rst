.. _arch_overview_http_filters:

HTTP filters
============

Much like the :ref:`network level filter <arch_overview_network_filters>` stack, Envoy supports an
HTTP level filter stack within the connection manager.

Filters can be written that operate on HTTP level messages without knowledge of the underlying physical
protocol (HTTP/1.1, HTTP/2, etc.) or multiplexing capabilities.

HTTP filters can be downstream HTTP filters, associated with a given listener and doing stream processing on each
downstream request before routing, or upstream HTTP filters, associated with a given cluster and doing stream processing once per upstream request, after the router filter.

There are three types of HTTP level filters:

**Decoder**
    Decoder filters are invoked when the connection manager is decoding parts of the
    request stream (headers, body, and trailers).
**Encoder**
    Encoder filters are invoked when the connection manager is about to encode parts of
    the response stream (headers, body, and trailers).
**Decoder/Encoder**
    Decoder/Encoder filters are invoked both when the connection manager is
    decoding parts of the request stream and when the connection manager is about to encode parts of
    the response stream.

The API for HTTP level filters allows the filters to operate without knowledge of the underlying
protocol.

Like network level filters, HTTP filters can stop and continue iteration to subsequent
filters. This allows for more complex scenarios such as health check handling, calling a rate
limiting service, buffering, routing, generating statistics for application traffic such as
DynamoDB, etc.

HTTP level filters can also share state (static and dynamic) among themselves within the context
of a single request stream. Refer to :ref:`data sharing between filters <arch_overview_data_sharing_between_filters>`
for more details.

.. tip::
   See the HTTP filters :ref:`configuration <config_http_filters>` and
   :ref:`protobuf <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_filters>`
   sections for reference documentation.

   See :ref:`here <extension_category_envoy.filters.http>` for included filters.

.. _arch_overview_http_filters_ordering:

Filter ordering
---------------

Filter ordering in the :ref:`http_filters <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_filters>`
field matters. If filters are configured in the following order (and assuming all three filters are
decoder/encoder filters):

.. code-block:: yaml

  http_filters:
    - A
    - B
    # The last configured filter has to be a terminal filter, as determined by the
    # NamedHttpFilterConfigFactory::isTerminalFilterByProto(config, context) function. This is most likely the router
    # filter.
    - C

The connection manager will invoke decoder filters in the order: ``A``, ``B``, ``C``.
On the other hand, the connection manager will invoke encoder filters in the **reverse**
order: ``C``, ``B``, ``A``.

Conditional filter configuration
--------------------------------

There is some support for having the filter configuration used change based on the incoming
request. See the :ref:`composite filter <config_http_filters_composite>` for details on how to
configure a match tree that can resolve filter configuration to use for a given request.

.. _arch_overview_http_filters_route_mutation:

Filter route mutation
---------------------

During downstream HTTP filter chain processing, when ``decodeHeaders()`` is invoked by a filter, the
connection manager performs route resolution and sets a *cached route* pointing to an upstream
cluster.

Downstream HTTP filters have the capability to directly mutate this *cached route* after route resolution, via the
``setRoute`` callback and :repo:`DelegatingRoute <source/common/router/delegating_route_impl.h>`
mechanism.

A filter may create a derived/child class of ``DelegatingRoute`` to override specific methods
(for example, the route’s timeout value or the route entry’s cluster name) while preserving
the rest of the properties/behavior of the base route that the ``DelegatingRoute`` wraps around.
Then, ``setRoute`` can be invoked to manually set the cached route to this ``DelegatingRoute``
instance. An example of such a derived class can be found in :repo:`ExampleDerivedDelegatingRoute
<test/test_common/delegating_route_utility.h>`.

If no other filters in the chain modify the cached route selection (for example, a common operation
that filters do is ``clearRouteCache()``, and ``setRoute`` will not survive that), this route
selection makes its way to the router filter which finalizes the upstream cluster that the request
will be forwarded to.

.. _arch_overview_http_filters_per_filter_config:

Route specific config
---------------------

The per filter config map can be used to provide
:ref:`route <envoy_v3_api_field_config.route.v3.Route.typed_per_filter_config>` or
:ref:`virtual host <envoy_v3_api_field_config.route.v3.VirtualHost.typed_per_filter_config>` or
:ref:`route configuration <envoy_v3_api_field_config.route.v3.RouteConfiguration.typed_per_filter_config>`
specific config for http filters.


The key of the per filter config map should match the :ref:`filter config name
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.name>`.


For example, given following http filter config:

.. code-block:: yaml

  http_filters:
  - name: custom-filter-name-for-lua # Custom name be used as filter config name
    typed_config: { ... }
  - name: envoy.filters.http.buffer # Canonical name be used as filter config name
    typed_config: { ... }

The ``custom-filter-name-for-lua`` and ``envoy.filters.http.buffer`` will be used as the key to lookup
related per filter config.


Use of per filter config map is filter specific. See the :ref:`HTTP filter documentation <config_http_filters>`
for if and how it is utilized for every filter.

.. _arch_overview_http_filters_route_based_filter_chain:

Route based filter chain
------------------------

There is support for having different filter chains for different routes. There are two different modes for this:

* Disabling a filter in the filter chain for specific routes.
* Overriding a filter in the filter chain that is disabled by default and enabling it for specific
  routes.

By default, the filter chain is the same for all routes and all filters are enabled. However, a filter
can be disabled for specific routes by using the :ref:`FilterConfig <envoy_v3_api_msg_config.route.v3.FilterConfig>`
and setting the :ref:`disabled field <envoy_v3_api_field_config.route.v3.FilterConfig.disabled>` in the
per filter config map in the route configuration. See the
:ref:`Route specific config <arch_overview_http_filters_per_filter_config>` section for more details.

For example, given following http filter config:

.. code-block:: yaml

  http_filters:
  - name: buffer
    typed_config: { ... }
  - name: lua
    typed_config: { ... }

Both the ``buffer`` and ``lua`` filters are enabled by default. If we want to disable the ``buffer`` filter
for a specific route, we can set the per filter config map in the route configuration:

.. code-block:: yaml

  typed_per_filter_config:
    buffer:
      "@type": type.googleapis.com/envoy.config.route.v3.FilterConfig
      disabled: true

In addition, we can set a filter to be disabled by default by setting the :ref:`disabled field
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.disabled>`
in the HttpFilter configuration and then enable it for specific routes if needed.

For example, given following http filter config:

.. code-block:: yaml

  http_filters:
  - name: buffer
    typed_config: { ... }
    disabled: true
  - name: lua
    typed_config: { ... }
    disabled: true

Both the ``buffer`` and ``lua`` filters are disabled by default. If we want to enable one of them
for a specific route, we can set per filter config map in the route configuration:

.. code-block:: yaml

  typed_per_filter_config:
    lua:
      "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.LuaPerRoute
      name: my_lua_script


Legitimate route-specific configuration for filter (like the above ``lua`` filter) is valid way to
enable the filter for the route.
