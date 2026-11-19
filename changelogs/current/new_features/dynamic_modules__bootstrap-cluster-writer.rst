Added a cluster writer to the dynamic module bootstrap extension. Once a module calls
``envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_writer``, it can add, update and
remove clusters, and it learns when each write becomes active. See
:ref:`Cluster Writer <config_bootstrap_extensions_dynamic_modules_cluster_writer>`.
