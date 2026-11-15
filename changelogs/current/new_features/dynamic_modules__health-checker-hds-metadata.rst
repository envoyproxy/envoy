Added the ``envoy_dynamic_module_callback_health_checker_set_health_metadata`` callback to the
:ref:`dynamic module health checker
<envoy_v3_api_msg_extensions.health_checkers.dynamic_modules.v3.DynamicModuleHealthCheck>`. A module
can attach string key/value pairs to each health check result, and HDS forwards them to the
management server in :ref:`health_metadata
<envoy_v3_api_field_service.health.v3.EndpointHealth.health_metadata>`. The pairs are replaced on
every check. The ``http_status_code`` key is reserved and rejected.
