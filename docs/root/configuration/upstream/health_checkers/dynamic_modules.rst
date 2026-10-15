.. _config_health_checkers_dynamic_modules:

Dynamic modules
===============

The dynamic modules health checker is a custom health checker (with :code:`envoy.health_checkers.dynamic_modules`
as name) that delegates the actual health check to a :ref:`dynamic module <arch_overview_dynamic_modules>`.
Envoy drives the standard per-host :ref:`interval <envoy_v3_api_field_config.core.v3.HealthCheck.interval>`,
:ref:`timeout <envoy_v3_api_field_config.core.v3.HealthCheck.timeout>` and
:ref:`healthy <envoy_v3_api_field_config.core.v3.HealthCheck.healthy_threshold>`/
:ref:`unhealthy <envoy_v3_api_field_config.core.v3.HealthCheck.unhealthy_threshold>` thresholds. On each interval
the module performs the check (optionally on its own thread) and reports the host's health status back to Envoy,
which applies it on the main thread.

An example setting for :ref:`custom_health_check <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>`
as a dynamic modules health checker is shown below:

.. code-block:: yaml

  custom_health_check:
    name: envoy.health_checkers.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.health_checkers.dynamic_modules.v3.DynamicModuleHealthCheck
      dynamic_module_config:
        name: my_health_checker_module
      health_checker_name: my_health_checker
      health_checker_config:
        "@type": type.googleapis.com/google.protobuf.StringValue
        value: example-config

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.health_checkers.dynamic_modules.v3.DynamicModuleHealthCheck>`
