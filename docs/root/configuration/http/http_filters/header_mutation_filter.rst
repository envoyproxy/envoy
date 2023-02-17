.. _config_http_filters_header_mutation:

Header Mutation
===============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.header_mutation.v3.HeaderMutation``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.header_mutation.v3.HeaderMutation>`

This is a filter that can be used to add, remove, append, or update HTTP headers. It can be added in any position in the filter chain
and used as downstream or upstream HTTP filter. The filter can be configured to apply the header mutations to the request, response, or both.
