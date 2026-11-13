Added support for deferred in-memory virtual host and route instantiation via
:ref:`route_manager.enable_deferred_virtual_host_creation
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.RouteManager.enable_deferred_virtual_host_creation>`,
reducing memory footprint for large route tables by delaying virtual host construction until a
request matches its domain.
