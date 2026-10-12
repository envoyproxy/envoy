Added :ref:`clear_route_cache_headers
<envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.clear_route_cache_headers>` to the
HTTP ext_authz filter. When set and :ref:`clear_route_cache
<envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.clear_route_cache>` is ``true``, an
``OK`` authorization response clears the route cache only when it sets, appends, or removes one of
the listed request headers, or mutates a query parameter, instead of on any request mutation.
