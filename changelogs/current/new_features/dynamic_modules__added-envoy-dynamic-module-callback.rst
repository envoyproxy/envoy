Added ``envoy_dynamic_module_callback_cluster_lb_context_get_filter_state_bytes`` and
``envoy_dynamic_module_callback_cluster_lb_context_get_filter_state_typed`` ABI callbacks so
that a dynamic-module cluster's load balancer can read filter state set by an upstream HTTP
filter (or any other producer) when picking a host. The Rust SDK exposes these as
``ClusterLbContext::get_filter_state_bytes`` and ``ClusterLbContext::get_filter_state_typed``.
