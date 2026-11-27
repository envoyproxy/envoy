Added :ref:`connection_aware_lb_config
<envoy_v3_api_field_extensions.load_balancing_policies.common.v3.ConnectionAwareLbConfig>`
to the ``ROUND_ROBIN`` and ``LEAST_REQUEST`` load balancing policies, which biases host
selection toward hosts that already have a ready connection on the current worker thread.
When the load balancer picks a host with no ready connection, it re-picks up to
:ref:`host_selection_retry_max_attempts
<envoy_v3_api_field_extensions.load_balancing_policies.common.v3.ConnectionAwareLbConfig.host_selection_retry_max_attempts>`
times (default 2), and chooses the last host it picked when no host is warm. Most useful with the
eager preconnect floor (``preconnect_policy.eager_preconnect_floor``), which primes and maintains
the connections. Emits stats ``lb_connection_aware_skipped_cold`` and
``lb_connection_aware_selected_cold``.