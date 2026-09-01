Added a dynamic modules cluster specifier extension
(``envoy.router.cluster_specifier_plugin.dynamic_modules``) that lets a dynamic module select the
upstream cluster for a request and replace the timeout, idle timeout, priority, request body buffer
limit, retry policy, metadata match criteria and request mirroring policies of the matched route.
The selection context exposes the request headers, stream info attributes, dynamic metadata, the
route name, and the random value Envoy generated for cluster selection, and the module is invoked
again whenever a filter refreshes the route cluster. The Rust SDK exposes this through the
``cluster_specifier`` module and the ``cluster_specifier:`` arm of ``declare_all_init_functions!``.
See
:ref:`DynamicModuleClusterSpecifier <envoy_v3_api_msg_extensions.router.cluster_specifiers.dynamic_modules.v3.DynamicModuleClusterSpecifier>`
for configuration details.
