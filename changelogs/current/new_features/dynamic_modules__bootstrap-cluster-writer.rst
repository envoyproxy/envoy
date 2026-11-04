Added a cluster writer to the dynamic module bootstrap extension. With :ref:`cluster_writer
<envoy_v3_api_field_extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension.cluster_writer>`
set, a module can add, update and remove clusters, and it learns when each write becomes active.
See :ref:`Cluster Writer <config_bootstrap_extensions_dynamic_modules_cluster_writer>`.
