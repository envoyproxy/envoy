Added a dynamic modules cluster specifier extension
(``envoy.router.cluster_specifier_plugin.dynamic_modules``) that lets a dynamic module select the
upstream cluster for a request and replace the timeout, idle timeout, priority, request body buffer
limit, cluster not found response code, hash policy, retry policy, metadata match criteria and request
mirroring policies of the matched route.
The selection context exposes the request headers, stream info attributes, dynamic metadata, the
route name, the random value Envoy generated for cluster selection, and a routability query that
reports host counts for a named cluster from the current worker, and the module is invoked
again whenever a filter refreshes the route cluster. Custom counters, gauges and histograms can be
defined during configuration and recorded during selection, and are emitted under the
``metrics_namespace`` prefix of ``DynamicModuleConfig``. The Rust SDK exposes this through the
``cluster_specifier`` module and the ``cluster_specifier:`` arm of ``declare_all_init_functions!``.
See
:ref:`DynamicModuleClusterSpecifier <envoy_v3_api_msg_extensions.router.cluster_specifiers.dynamic_modules.v3.DynamicModuleClusterSpecifier>`
for configuration details.
