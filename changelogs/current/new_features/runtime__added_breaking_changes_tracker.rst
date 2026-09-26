Added the ``BreakingChangesTracker`` class to track and observe breaking changes in Envoy.
Operators can enable observability for breaking changes via the
:ref:`enable_breaking_changes_observability
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.enable_breaking_changes_observability>` flag in
:ref:`Bootstrap <envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` and log observed breaking changes
in access logs using ``%FILTER_STATE(envoy.breaking_changes_tracker:PLAIN)%``.
See :ref:`Introducing a breaking change <arch_overview_runtime_breaking_changes>` and
:ref:`Enabling observability for breaking changes
<arch_overview_runtime_breaking_changes_observability>`.
