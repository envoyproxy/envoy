.. _arch_overview_runtime:

Runtime configuration
=====================

Envoy supports “runtime” configuration (also known as "feature flags"). :ref:`Runtime configuration
<config_runtime>` can be used to modify various server settings without restarting Envoy. The
runtime settings that are available depend on how the server is configured. Runtime guards which are
not expected to be transient are documented in the relevant sections of the :ref:`configuration
guide <config>`.

Runtime guards are also used as a mechanism to disable new behavior or risky changes not otherwise
guarded by configuration. Such changes will tend to introduce a temporary runtime guard that can be
used to disable the new behavior/code path. The names of these runtime guards will be included in
the release notes alongside an explanation of the change that warranted the runtime guard.

Due to this usage of runtime guards, some deployments might find it useful to set up dynamic
(filesystem, RTDS, etc.) :ref:`runtime configuration <config_runtime>` as a safety measure to be
able to quickly disable the new behavior without having to revert to an older version of Envoy or
redeploy it with a new set of static runtime flags.

See Runtime :ref:`configuration <config_runtime>` as well as the
`contributing guide <https://github.com/envoyproxy/envoy/blob/main/CONTRIBUTING.md#runtime-guarding>`_
for more information.

.. _arch_overview_runtime_breaking_changes:

Introducing a breaking change
-----------------------------

"Breaking change" is a code change that may have observable impact on existing production traffic.
When introducing a breaking change in Envoy, it desirable to surface in observability potential impact of
enabling the chanmge. Operators can assess the impact via access logs and metrics before older behaviors
are modified or removed.

Adding a breaking change flag introduces Envoy reloadabale flag with the ``envoy.reloadable_features.<flag_name>``
name and puts tracking state in the ``BreakingChangesTracker`` class. Breaking changes are enable by setting
the ``envoy.reloadable_features.<flag_name>`` flag in Envoy Runtime.

To introduce and track a breaking change:

1. Add a new flag to :repo:`source/common/runtime/breaking_changes_flags.h`.
   Define the flag inside the ``ALL_BREAKING_CHANGES`` macro list using either the ``ENABLED`` or
   ``DISABLED`` macro:

   .. code-block:: cpp

     #define ALL_BREAKING_CHANGES(ENABLED, DISABLED) \
       ENABLED(my_breaking_change)

   Use the ENABLED macro for flags that are enabled by default and DISABLED macro for flags that
   are disabled by default.

2. At the point of breaking change introduction, add the ``OBSERVED_BREAKING_CHANGE`` macro:

   .. code-block:: cpp

     OBSERVED_BREAKING_CHANGE(my_breaking_change, stream_info.filterState());

   This records the occurrence in the ``envoy.breaking_changes_tracker`` filter state object.
   The tracked breaking changes can then be logged in access logs using
   ``%FILTER_STATE(envoy.breaking_changes_tracker:PLAIN)%``.

3. To check in the code whether the breaking change is enabled or disabled, use the
   ``BREAKING_CHANGE_ENABLED`` macro:

   .. code-block:: cpp

     if (BREAKING_CHANGE_ENABLED(my_breaking_change)) {
       // Code path when the breaking change is enabled.
     } else {
       // Code path when the breaking change is disabled.
     }

.. _arch_overview_runtime_breaking_changes_observability:

Enabling observability for breaking changes
-------------------------------------------

Operators can enable observability for breaking changes to monitor their occurrence in production
traffic. Observability for breaking changes is disabled by default.

To enable observability:

1. Enable the ``envoy.reloadable_features.breaking_change_observability_enabled`` runtime flag via
   :ref:`runtime configuration <config_runtime>` (for example, in the static bootstrap layer or
   dynamically via RTDS):

   .. code-block:: yaml

      layered_runtime:
        layers:
          - name: static_layer
            static_layer:
              envoy.reloadable_features.breaking_change_observability_enabled: true

2. Configure access logging to include the ``envoy.breaking_changes_tracker`` filter state using
   ``%FILTER_STATE(envoy.breaking_changes_tracker:PLAIN)%``:

   .. code-block:: yaml

      access_log:
        - name: envoy.access_loggers.file
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
            path: /dev/stdout
            log_format:
              text_format_source:
                inline_string: "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" %RESPONSE_CODE% breaking_changes=\"%FILTER_STATE(envoy.breaking_changes_tracker:PLAIN)%\"\n"

When the ``envoy.reloadable_features.breaking_change_observability_enabled`` runtime flag is enabled,
breaking changes encountered during request processing will be recorded in the
``envoy.breaking_changes_tracker`` filter state and included in the access log. If the runtime flag
is disabled, no breaking changes are recorded in filter state.

