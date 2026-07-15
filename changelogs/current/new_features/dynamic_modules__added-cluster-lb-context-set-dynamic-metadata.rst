Added the ``envoy_dynamic_module_callback_cluster_lb_context_set_dynamic_metadata_number`` and
``envoy_dynamic_module_callback_cluster_lb_context_set_dynamic_metadata_string`` ABI callbacks so
custom cluster load balancers can annotate the current request with dynamic metadata at request
time inside ``envoy_dynamic_module_on_cluster_lb_choose_host``, mirroring the existing HTTP filter
setters. The metadata is readable later on the same request (for example via
``%DYNAMIC_METADATA(namespace:key)%``). The Rust SDK exposes these as
``ClusterLbContext::set_dynamic_metadata_number`` and ``ClusterLbContext::set_dynamic_metadata_string``.
