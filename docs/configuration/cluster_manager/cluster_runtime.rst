.. _config_cluster_manager_cluster_runtime:

Runtime
=======

Upstream clusters support the following runtime settings:

Active health checking
----------------------

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

.. _config_cluster_manager_cluster_runtime_outlier_detection:

Outlier detection
-----------------

See the outlier detection :ref:`architecture overview <arch_overview_outlier_detection>` for more
information on outlier detection.

outlier_detection.consecutive_5xx
  The number of consecutive 5xx responses before a consecutive 5xx ejection occurs. Defaults to 5.

outlier_detection.interval_ms
  The time interval between ejection analysis sweeps. This can result in both new ejections as well
  as hosts being returned to service. Defaults to 10000ms or 10s.

outlier_detection.base_ejection_time_ms
  The base time that a host is ejected for. The real time is equal to the base time multiplied by
  the number of times the host has been ejected. Defaults to 30000ms or 30s.

outlier_detection.max_ejection_percent
  The maximum % of an upstream cluster that can be ejected due to outlier detection. Defaults to
  10%.

outlier_detection.enforcing
  The % chance that a host will be actually ejected when an outlier status is detected. This setting
  can be used to disable ejection or to ramp it up slowly. Defaults to 100.

Core
----

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
  :ref:`Max connections circuit breaker setting <config_cluster_manager_cluster_circuit_breakers_max_connections>`

circuit_breakers.<cluster_name>.<priority>.max_pending_requests
  :ref:`Max pending requests circuit breaker setting <config_cluster_manager_cluster_circuit_breakers_max_pending_requests>`

circuit_breakers.<cluster_name>.<priority>.max_requests
  :ref:`Max requests circuit breaker setting <config_cluster_manager_cluster_circuit_breakers_max_requests>`

circuit_breakers.<cluster_name>.<priority>.max_retries
  :ref:`Max retries circuit breaker setting <config_cluster_manager_cluster_circuit_breakers_max_retries>`
