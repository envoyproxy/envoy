Fixed a bug where the ``envoy_dynamic_module_on_http_filter_per_route_config_destroy`` hook of a
:ref:`dynamic module <envoy_v3_api_msg_extensions.filters.http.dynamic_modules.v3.DynamicModuleFilterPerRoute>`
could run on a worker thread, and the module could be unloaded there, when an RDS update replaced
the route configuration.
