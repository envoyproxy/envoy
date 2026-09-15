Added the ``BreakingChangesTracker`` class to track and observe breaking changes in Envoy.
Operators can enable observability for breaking changes via the
``envoy.reloadable_features.breaking_change_observability_enabled`` runtime flag and log observed
breaking changes in access logs using ``%FILTER_STATE(envoy.breaking_changes_tracker:PLAIN)%``.
See :ref:`Introducing a breaking change <arch_overview_runtime_breaking_changes>` and
:ref:`Enabling observability for breaking changes
<arch_overview_runtime_breaking_changes_observability>`.
