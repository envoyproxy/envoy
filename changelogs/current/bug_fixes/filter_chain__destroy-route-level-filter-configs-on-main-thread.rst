Fixed a race where the route level configuration of the :ref:`filter chain
<envoy_v3_api_msg_extensions.filters.http.filter_chain.v3.FilterChainConfigPerRoute>` filter could
be destroyed on a worker thread when an RDS update replaced the route configuration. The embedded
filter chain, and the filter configuration providers it owns, are now released on the main thread.
