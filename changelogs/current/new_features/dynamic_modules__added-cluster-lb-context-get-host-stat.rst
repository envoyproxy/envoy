Added the ``envoy_dynamic_module_callback_cluster_lb_context_get_host_stat`` ABI callback so
custom cluster load balancers can read per-host counters and gauges at request time inside
``envoy_dynamic_module_on_cluster_lb_choose_host``. The Rust SDK exposes this as
``ClusterLbContext::get_host_stat``.
