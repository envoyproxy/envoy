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

upstream.zone_routing.enabled
  % of requests that will be routed to the same upstream zone. Defaults to 100% of requests.

upstream.zone_routing.percent_diff
  Zone aware routing will be used only if the percent of upstream hosts in the same zone is within
  percent_diff of expected. Expected is calculated as 100 / number_of_zones. This prevents Envoy
  from using same zone routing if the zones are not balanced well.

upstream.zone_routing.healthy_panic_threshold
  Defines the :ref:`zone healthy panic threshold <arch_overview_load_balancing_zone_panic_threshold>`
  percentage. Defaults to 80%. If the % of healthy hosts in the current zone falls below this %
  all healthy hosts will be used for routing.
