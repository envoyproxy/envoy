Fixed the upstream HTTP TCP bridge (``envoy.upstreams.http.dynamic_modules``) loading its dynamic
module and creating its bridge configuration on worker threads, where two workers could run
``envoy_dynamic_module_on_program_init`` at the same time or use the module before it returned. The
bridge now loads its module and calls ``envoy_dynamic_module_on_upstream_http_tcp_bridge_config_new``
once on the main thread when the cluster is created, as the ABI documents, and a module or bridge
configuration that fails rejects the cluster instead of failing its requests.
