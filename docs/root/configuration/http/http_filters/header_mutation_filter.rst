.. _config_http_filters_header_mutation:

Header Mutation
===============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.header_mutation.v3.HeaderMutation>`

This filter can add, remove, append, or update HTTP headers and trailers. It can be placed anywhere in the HTTP filter chain and used as a downstream or upstream HTTP filter. The filter can be configured to apply mutations to the request, the response, or both.

In most cases, this filter is a more flexible alternative to the ``request_headers_to_add``, ``request_headers_to_remove``,
``response_headers_to_add``, and ``response_headers_to_remove`` fields in the :ref:`route configuration <envoy_v3_api_msg_config.route.v3.RouteConfiguration>`.

The filter provides complete control over the position and ordering of mutations. It may influence later route selection if a subsequent filter clears the route cache.

When configured, the filter can also mutate query parameters parsed from the ``:path`` header. Query parameter mutations are applied after request header mutations and before the request is forwarded to the next filter in the chain. See :ref:`query_parameter_mutations <envoy_v3_api_field_extensions.filters.http.header_mutation.v3.Mutations.query_parameter_mutations>`.

Upstream Usage
--------------

This filter can also be used as an upstream HTTP filter to mutate request headers after load balancing and host selection.

Per-Route Configuration
-----------------------

Per-route overrides may be supplied via :ref:`HeaderMutationPerRoute <envoy_v3_api_msg_extensions.filters.http.header_mutation.v3.HeaderMutationPerRoute>`. If per-route configuration is applied at multiple route levels, all configured mutations are evaluated. By default, evaluation proceeds from most specific (route entry) to least specific (route configuration), and later mutations may override earlier ones. This order can be changed by setting :ref:`most_specific_header_mutations_wins <envoy_v3_api_field_extensions.filters.http.header_mutation.v3.HeaderMutation.most_specific_header_mutations_wins>` to ``true``, causing the most specific level to be evaluated last.

Execution and Local Replies
---------------------------

.. note::

  As an encoder filter, Header Mutation follows the standard execution rules for local replies. Response headers are not unconditionally added in cases where the filter would be bypassed.

Security Considerations
-----------------------

.. attention::

  When filters later in the chain clear the route cache, mutations performed by this filter may affect subsequent route selection. Review the implications carefully when header or query parameter mutations influence routing. See :ref:`Filter route mutation security considerations <arch_overview_http_filters_route_mutation>`.

.. seealso::

   :ref:`Header Mutation filter (proto file) <envoy_v3_api_file_envoy/extensions/filters/http/header_mutation/v3/header_mutation.proto>`
      ``HeaderMutation`` API reference.

   :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.header_mutation.v3.HeaderMutation>`
      Configuration message reference.
