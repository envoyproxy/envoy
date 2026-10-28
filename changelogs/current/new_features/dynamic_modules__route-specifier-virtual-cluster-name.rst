Added ``set_virtual_cluster_name`` to the dynamic modules route specifier, also in the Rust SDK, so
that a module can count a request in one of the :ref:`virtual_cluster_names
<envoy_v3_api_field_extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier.virtual_cluster_names>`
the specifier declares rather than in the virtual cluster the virtual host matches. The request uses
the stats of the virtual cluster of that name on the virtual host.
