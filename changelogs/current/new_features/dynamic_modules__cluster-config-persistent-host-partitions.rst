Added ``envoy_dynamic_module_callback_cluster_config_use_persistent_host_partitions``
so a cluster module can opt its cluster into :ref:`use_persistent_host_partitions
<envoy_v3_api_field_extensions.clusters.dynamic_modules.v3.ClusterConfig.use_persistent_host_partitions>`
during ``envoy_dynamic_module_on_cluster_config_new``, without the field in the cluster's
configuration.
