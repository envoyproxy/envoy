Added a new dynamic modules health checker extension (``envoy.health_checkers.dynamic_modules``)
that delegates active health checking to a dynamic module loaded via ``dlopen``. Envoy drives the
standard per-host interval/timeout timers and healthy/unhealthy thresholds, while the module
performs the check (optionally on its own thread) and reports each host's health back through a
thread-safe reporter. The Rust SDK exposes this through ``HealthCheckerConfig`` and
``HealthCheckerSession`` traits and a matching health checker init function. Configured via
:ref:`DynamicModuleHealthCheck
<envoy_v3_api_msg_extensions.health_checkers.dynamic_modules.v3.DynamicModuleHealthCheck>`.
