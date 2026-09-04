Added a dynamic modules route provider extension
(``envoy.router.route_provider.dynamic_modules``) that lets a dynamic module own route selection for
a virtual host through the new :ref:`route_provider
<envoy_v3_api_field_config.route.v3.VirtualHost.route_provider>` field. The module selects one of the
route templates for a request and replaces the cluster, the timeouts, the priority, the request body
buffer limit, the route metadata, the request and response headers, the path, the retry, hash,
metadata match, request mirroring and rate limit policies, the per-filter configuration and the
route-level tracing of the selected template, or resolves the request to a direct response or a
redirect.
The selection context exposes the request headers, the stream info attributes and the random value
Envoy generated for route resolution. The Rust SDK exposes this through the ``route_provider`` module
and the ``route_provider:`` arm of ``declare_all_init_functions!``.
See
:ref:`DynamicModuleRouteProvider <envoy_v3_api_msg_extensions.router.route_providers.dynamic_modules.v3.DynamicModuleRouteProvider>`
for configuration details.
