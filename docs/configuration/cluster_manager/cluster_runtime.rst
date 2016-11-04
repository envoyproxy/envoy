.. _config_cluster_manager_cluster_runtime:

Runtime
=======

Upstream clusters support the following runtime settings:

health_check.min_interval
  Min value for the health checking :ref:`interval <config_cluster_manager_cluster_hc_interval>`.
  Default value is 0. The health checking interval will be between *min_interval* and
  *max_interval*.

health_check.max_interval
  Max value for the health checking :ref:`interval <config_cluster_manager_cluster_hc_interval>`.
  Default value is MAX_INT. The health checking interval will be between *min_interval* and
  *max_interval*.

health_check.verify_cluster
  What % of health check requests will be verified against the :ref:`expected upstream service
  <config_cluster_manager_cluster_hc_service_name>` as the :ref:`health check filter
  <arch_overview_health_checking_filter>` will write the remote service cluster into the response.

upstream.healthy_panic_threshold
  Sets the :ref:`panic threshold <arch_overview_load_balancing_panic_threshold>` percentage.
  Defaults to 50%.

upstream.use_http2
  Whether the cluster utilizes the *http2* :ref:`feature <config_cluster_manager_cluster_features>`
  if configured. Set to 0 to disable HTTP/2 even if the feature is configured. Defaults to enabled.

upstream.weight_enabled
  Binary switch to turn on or off weighted load balancing. If set to non 0, weighted load balancing
  is enabled. Defaults to enabled.

.. _config_cluster_manager_cluster_runtime_zone_routing:

upstream.zone_routing.enabled
  % of requests that will be routed to the same upstream zone. Defaults to 100% of requests.

upstream.zone_routing.min_cluster_size
  Minimal size of the upstream cluster for which zone aware routing can be attempted. Default value
  is 6. If the upstream cluster size is smaller than *min_cluster_size* zone aware routing will not
  be performed.

circuit_breakers.<cluster_name>.<priority>.max_connections
  :ref:`Max connections circuit breaker setting <config_cluster_manager_cluster_circuit_breakers_max_connections>`

circuit_breakers.<cluster_name>.<priority>.max_pending_requests
  :ref:`Max pending requests circuit breaker setting <config_cluster_manager_cluster_circuit_breakers_max_pending_requests>`

circuit_breakers.<cluster_name>.<priority>.max_requests
  :ref:`Max requests circuit breaker setting <config_cluster_manager_cluster_circuit_breakers_max_requests>`

circuit_breakers.<cluster_name>.<priority>.max_retries
  :ref:`Max retries circuit breaker setting <config_cluster_manager_cluster_circuit_breakers_max_retries>`
