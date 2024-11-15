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
