.. _config_cluster_manager_cluster_runtime:

Runtime
=======

Upstream clusters support the following runtime settings:

Active health checking
----------------------

health_check.min_interval
  Min value for the health checking :ref:`interval <envoy_api_field_core.HealthCheck.interval>`.
  Default value is 1 ms. The effective health check interval will be no less than 1ms. The health
  checking interval will be between *min_interval* and *max_interval*.

health_check.max_interval
  Max value for the health checking :ref:`interval <envoy_api_field_core.HealthCheck.interval>`.
  Default value is MAX_INT. The effective health check interval will be no less than 1ms. The health
  checking interval will be between *min_interval* and *max_interval*.

health_check.verify_cluster
  What % of health check requests will be verified against the :ref:`expected upstream service
  <envoy_api_field_core.HealthCheck.HttpHealthCheck.service_name>` as the :ref:`health check filter
  <arch_overview_health_checking_filter>` will write the remote service cluster into the response.

.. _config_cluster_manager_cluster_runtime_outlier_detection:

Outlier detection
-----------------

See the outlier detection :ref:`architecture overview <arch_overview_outlier_detection>` for more
information on outlier detection. The runtime parameters supported by outlier detection are the
same as the :ref:`static configuration parameters <envoy_api_msg_cluster.OutlierDetection>`, namely:

outlier_detection.consecutive_5xx
  :ref:`consecutive_5XX
  <envoy_api_field_cluster.OutlierDetection.consecutive_5xx>`
  setting in outlier detection

outlier_detection.consecutive_gateway_failure
  :ref:`consecutive_gateway_failure
  <envoy_api_field_cluster.OutlierDetection.consecutive_gateway_failure>`
  setting in outlier detection

outlier_detection.interval_ms
  :ref:`interval_ms
  <envoy_api_field_cluster.OutlierDetection.interval>`
  setting in outlier detection

outlier_detection.base_ejection_time_ms
  :ref:`base_ejection_time_ms
  <envoy_api_field_cluster.OutlierDetection.base_ejection_time>`
  setting in outlier detection

outlier_detection.max_ejection_percent
  :ref:`max_ejection_percent
  <envoy_api_field_cluster.OutlierDetection.max_ejection_percent>`
  setting in outlier detection

outlier_detection.enforcing_consecutive_5xx
  :ref:`enforcing_consecutive_5xx
  <envoy_api_field_cluster.OutlierDetection.enforcing_consecutive_5xx>`
  setting in outlier detection

outlier_detection.enforcing_consecutive_gateway_failure
  :ref:`enforcing_consecutive_gateway_failure
  <envoy_api_field_cluster.OutlierDetection.enforcing_consecutive_gateway_failure>`
  setting in outlier detection

outlier_detection.enforcing_success_rate
  :ref:`enforcing_success_rate
  <envoy_api_field_cluster.OutlierDetection.enforcing_success_rate>`
  setting in outlier detection

outlier_detection.success_rate_minimum_hosts
  :ref:`success_rate_minimum_hosts
  <envoy_api_field_cluster.OutlierDetection.success_rate_minimum_hosts>`
  setting in outlier detection

outlier_detection.success_rate_request_volume
  :ref:`success_rate_request_volume
  <envoy_api_field_cluster.OutlierDetection.success_rate_request_volume>`
  setting in outlier detection

outlier_detection.success_rate_stdev_factor
  :ref:`success_rate_stdev_factor
  <envoy_api_field_cluster.OutlierDetection.success_rate_stdev_factor>`
  setting in outlier detection

Core
----

upstream.healthy_panic_threshold
  Sets the :ref:`panic threshold <arch_overview_load_balancing_panic_threshold>` percentage.
  Defaults to 50%.

upstream.use_http2
  Whether the cluster utilizes the *http2* :ref:`protocol options <envoy_api_field_Cluster.http2_protocol_options>`
  if configured. Set to 0 to disable HTTP/2 even if the feature is configured. Defaults to enabled.

.. _config_cluster_manager_cluster_runtime_zone_routing:

Zone aware load balancing
-------------------------

upstream.zone_routing.enabled
  % of requests that will be routed to the same upstream zone. Defaults to 100% of requests.

upstream.zone_routing.min_cluster_size
  Minimal size of the upstream cluster for which zone aware routing can be attempted. Default value
  is 6. If the upstream cluster size is smaller than *min_cluster_size* zone aware routing will not
  be performed.

Circuit breaking
----------------

circuit_breakers.<cluster_name>.<priority>.max_connections
  :ref:`Max connections circuit breaker setting <envoy_api_field_cluster.CircuitBreakers.Thresholds.max_connections>`

circuit_breakers.<cluster_name>.<priority>.max_pending_requests
  :ref:`Max pending requests circuit breaker setting <envoy_api_field_cluster.CircuitBreakers.Thresholds.max_pending_requests>`

circuit_breakers.<cluster_name>.<priority>.max_requests
  :ref:`Max requests circuit breaker setting <envoy_api_field_cluster.CircuitBreakers.Thresholds.max_requests>`

circuit_breakers.<cluster_name>.<priority>.max_retries
  :ref:`Max retries circuit breaker setting <envoy_api_field_cluster.CircuitBreakers.Thresholds.max_retries>`
