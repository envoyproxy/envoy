Added the ``envoy_dynamic_module_callback_cluster_lb_context_set_filter_state_bytes`` and
``envoy_dynamic_module_callback_cluster_lb_context_set_filter_state_typed`` ABI callbacks so
custom cluster load balancers can write filter state at request time inside
``envoy_dynamic_module_on_cluster_lb_choose_host``, mirroring the existing getters. The values use
``FilterState::LifeSpan::FilterChain`` so they are readable later on the same request (for example
via ``%FILTER_STATE(key:PLAIN)%``). The Rust SDK exposes these as
``ClusterLbContext::set_filter_state_bytes`` and ``ClusterLbContext::set_filter_state_typed``.
