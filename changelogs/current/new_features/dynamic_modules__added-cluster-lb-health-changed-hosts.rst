dynamic modules: added ``envoy_dynamic_module_callback_cluster_lb_get_member_update_health_changed_host_count``
and ``envoy_dynamic_module_callback_cluster_lb_get_member_update_health_changed_host``, which report
the hosts whose health changed in a membership update so a cluster load balancer can patch its
healthy set instead of rereading every host. The host count getters now read the counts without
building the host vectors.
