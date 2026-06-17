Added ABI primitives for publishing main-thread state out to worker threads on the cluster
dynamic-module extension: ``envoy_dynamic_module_callback_cluster_run_on_all_workers`` (with
the matching ``envoy_dynamic_module_on_cluster_worker_event`` hook) fans an event out to every
registered worker; ``envoy_dynamic_module_callback_cluster_worker_slot_set`` /
``envoy_dynamic_module_callback_cluster_worker_slot_get`` (with the matching
``envoy_dynamic_module_on_cluster_worker_slot_data_destroy`` cleanup hook) publish an opaque
payload into a worker thread-local slot and read it back. The Rust SDK adds matching
``Cluster::on_worker_event``, ``EnvoyCluster::run_on_all_workers``, and an
``EnvoyClusterWorkerSlotExt`` extension trait with type-safe ``worker_slot_set<T>(Arc<T>)`` /
``worker_slot_get<T>() -> Option<Arc<T>>``.
