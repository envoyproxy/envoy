Added :ref:`built_route_cache
<envoy_v3_api_field_extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier.built_route_cache>`
to the dynamic modules route specifier, a per worker cache of the routes a module builds from route
templates. A module looks a route up by key with ``lookup_built_route``, also in the Rust SDK, after
selecting a template, so a route that is a function of a small key is built once per worker rather
than once per request. Templates whose cluster is resolved per request are not cached, and a sampled
share of hits set by :ref:`verify_one_in
<envoy_v3_api_field_extensions.router.route_specifiers.dynamic_modules.v3.BuiltRouteCache.verify_one_in>`
is verified against what the module records. With ``config_has_built_route_cache``,
``has_built_route_cache`` in the Rust SDK, a module whose routes rely on the cache can reject a
configuration without one.
