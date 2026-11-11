Added support for checking overload manager load shed points from dynamic module HTTP, network,
listener and UDP listener filters. A filter gets a load shed point by name from its configuration
with ``envoy_dynamic_module_callback_http_filter_config_get_load_shed_point``,
``envoy_dynamic_module_callback_network_filter_config_get_load_shed_point``,
``envoy_dynamic_module_callback_listener_filter_config_get_load_shed_point`` or
``envoy_dynamic_module_callback_udp_listener_filter_config_get_load_shed_point``, and checks it with
``envoy_dynamic_module_callback_load_shed_point_should_shed_load``. The Rust SDK exposes this
through ``get_load_shed_point`` on the filter configurations and
``EnvoyLoadShedPoint::should_shed_load``, the Go SDK through ``GetLoadShedPoint`` and
``LoadShedPoint.ShouldShedLoad``, and the C++ SDK through ``getLoadShedPoint`` on the HTTP, network
and listener filter configurations and ``LoadShedPoint::shouldShedLoad``. A filter on a listener
that bypasses the overload manager gets no load shed point, unless the route configuration or the
Filter Chain Discovery Service (FCDS) created it, and an upstream HTTP filter gets none.
