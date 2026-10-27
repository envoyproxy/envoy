Added ``set_prefix_rewrite_rule`` to the dynamic modules route specifier and its Rust SDK. It records
a prefix rewrite that Envoy applies to the path the request carries when the request headers are
finalized, the way a configured route applies its :ref:`prefix_rewrite
<envoy_v3_api_field_config.route.v3.RouteAction.prefix_rewrite>`, so the route it produces is not
specific to the request it was built for. Requests for which neither the rule nor the regex rewrite
after it yields a path at that point are counted in ``rewrite_not_applied``.
