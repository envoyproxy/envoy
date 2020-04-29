.. _operations_runtime:

Runtime
=======

:ref:`Runtime configuration <config_runtime>` can be used to modify various server settings
without restarting Envoy. The runtime settings that are available depend on how the server is
configured. They are documented in the relevant sections of the :ref:`configuration guide <config>`.

Runtime flags are also used as a mechanism to disable new behavior or risky changes: such changes
will tend to introduce a runtime flag that can be used to disable the new behavior/code path. As such
some deployments might find it useful to set up dynamic runtime configuration as a safety measure to
be able to quickly disable this new/risky behavior without having to revert to an older version of
Envoy or redeploy it with a new set of static runtime flags.
