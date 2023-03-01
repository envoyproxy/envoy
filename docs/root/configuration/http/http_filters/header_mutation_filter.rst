.. _config_http_filters_header_mutation:

Header Mutation
===============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.header_mutation.v3.HeaderMutation>`

This is a filter that can be used to add, remove, append, or update HTTP headers. It can be added in any position in the filter chain
and used as downstream or upstream HTTP filter. The filter can be configured to apply the header mutations to the request, response, or both.


In most cases, this filter would be a more flexible alternative to the ``request_headers_to_add``, ``request_headers_to_remove``,
``response_headers_to_add``, and ``response_headers_to_remove`` fields in the :ref:`route configuration <envoy_v3_api_msg_config.route.v3.RouteConfiguration>`.
The filter provides complete control over the position and order of the header mutations. It can be used as upstream filter and
mutates the request headers after load balancing and host selection.


But note that this still has some limitations. In some corner cases, the header mutations may not be applied because the filter chain processing
is skipped.
