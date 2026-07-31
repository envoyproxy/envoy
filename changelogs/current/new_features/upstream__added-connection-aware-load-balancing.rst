Added :ref:`connection_aware_load_balancing
<envoy_v3_api_field_config.cluster.v3.Cluster.connection_aware_load_balancing>` which biases host
selection toward hosts that already have a ready connection on the current worker thread. When the
load balancer picks a host with no ready connection, it re-picks up to
:ref:`host_selection_retry_max_attempts
<envoy_v3_api_field_config.cluster.v3.Cluster.ConnectionAwareLbConfig.host_selection_retry_max_attempts>`
times (default 2), and chooses the last host it picked when no host is warm. Most useful with the eager
preconnect floor (``preconnect_policy.eager_preconnect_floor``), which primes and maintains the
connections. Emits the ``upstream_cx_lb_selected_warm`` and ``upstream_cx_lb_selected_cold`` stats.