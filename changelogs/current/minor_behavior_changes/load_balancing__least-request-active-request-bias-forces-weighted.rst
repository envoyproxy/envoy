The least request load balancer now uses weighted selection whenever
:ref:`active_request_bias
<envoy_v3_api_field_extensions.load_balancing_policies.least_request.v3.LeastRequest.active_request_bias>`
is explicitly configured, including when all host weights are equal. Previously the bias was
silently ignored in that case and hosts were selected by sampling ``choice_count`` random hosts.
Clusters that do not set ``active_request_bias`` are unaffected. This behavior can be reverted by
setting ``envoy.reloadable_features.least_request_lb_active_request_bias_forces_weighted`` to
``false``.
