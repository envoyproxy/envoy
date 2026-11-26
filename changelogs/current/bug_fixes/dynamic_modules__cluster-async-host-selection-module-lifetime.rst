Fixed the dynamic module cluster calling
``envoy_dynamic_module_on_cluster_lb_cancel_host_selection``, which runs the Rust SDK's
``AsyncHostSelectionHandle::cancel``, after its module was unloaded, when a request held an async
host selection past the removal of its cluster. A pending selection now keeps the module loaded
until that hook has run. The hook can still run after ``envoy_dynamic_module_on_cluster_lb_destroy``
and ``envoy_dynamic_module_on_cluster_destroy``, though always before
``envoy_dynamic_module_on_cluster_config_destroy``, so a module must not read load balancer or
cluster state through ``lb_module_ptr`` in it. The cluster configuration, with its
``envoy_dynamic_module_on_cluster_config_destroy`` hook, is now always destroyed on the main thread,
also when a worker releases it last. The ABI documentation now states that Envoy calls the cancel
hook once for every async handle, after a delivered result as well as on a cancellation, as every
release with the hook has done, so a module releases the handle there rather than when it calls the
completion callback.
