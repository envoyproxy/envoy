xDS gRPC subscription watch maps and subscription state machines now compress tracked xDS resource
names in memory using a shared symbol table to reduce steady-state memory usage in large xDS
deployments. This behavior can be reverted temporarily by setting the runtime guard
``envoy.reloadable_features.compress_xds_resource_names`` to ``false``.
