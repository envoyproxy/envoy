Added a dynamic modules route extension (``envoy.router.route_extension.dynamic_modules``) that lets
a dynamic module customize the route resolved for a request. The module reads the request headers and
the random value Envoy generated for the request, and returns a decision that keeps the route,
overrides the upstream cluster, or removes the route. When the decision overrides the route, the
module can also select a named :ref:`route action override
<envoy_v3_api_field_extensions.router.route_extension.dynamic_modules.v3.DynamicModuleRouteExtension.route_action_overrides>`
to replace the retry policy, metadata match criteria, request mirroring policies and hash policy of
the matched route with ones declared in the configuration. The Rust SDK exposes this through the
``route_extension`` module and the ``route_extension:`` arm of ``declare_all_init_functions!``. See
:ref:`DynamicModuleRouteExtension
<envoy_v3_api_msg_extensions.router.route_extension.dynamic_modules.v3.DynamicModuleRouteExtension>`
for configuration details.
