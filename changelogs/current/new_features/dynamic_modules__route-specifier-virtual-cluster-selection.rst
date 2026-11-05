Added ``set_virtual_cluster`` to the dynamic modules route specifier and its Rust SDK. It selects
the virtual cluster of a request from the names that :ref:`virtual_cluster_override
<envoy_v3_api_field_extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier.virtual_cluster_override>`
declares, and the selected virtual cluster uses the statistics of the virtual cluster of the same
name on the virtual host. ``prefer_selection`` chooses whether the router counts the request in the
selection or in the virtual cluster the virtual host matches, and the comparisons of the two are
counted in ``virtual_cluster_agree`` and ``virtual_cluster_disagree``.
