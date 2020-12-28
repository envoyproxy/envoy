.. _config_cluster_manager_cluster_runtime:

运行时
=======

上游集群支持如下的运行时设置：

主动健康检查
--------------

health_check.min_interval
  健康检查 :ref:`间隔 <envoy_v3_api_field_config.core.v3.HealthCheck.interval>` 的最小值。默认值是 1ms，
  有效的健康检查间隔将不能够小于 1ms。健康检查间隔的值在 *min_interval* 和 *max_interval* 之间。

health_check.max_interval
  健康检查 :ref:`间隔 <envoy_v3_api_field_config.core.v3.HealthCheck.interval>` 的最大值。默认值是 MAX_INT。
  有效的健康检查间隔将不能小于 1ms。健康检查间隔的值在 *min_interval* 和 *max_interval* 之间。

health_check.verify_cluster
  当 :ref:`健康检查过滤器 <arch_overview_health_checking_filter>` 将远程服务集群写到响应里面时，
  针对 :ref:`期望的上游服务 <envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.service_name_matcher>` 
  来确定健康检查请求被验证的百分比。

.. _config_cluster_manager_cluster_runtime_outlier_detection:

异常检测
----------

关于异常检测的更多信息，可以查看异常检测 :ref:`架构预览 <arch_overview_outlier_detection>`。异常检测支持的运行时参数和
:ref:`静态配置参数 <envoy_v3_api_msg_config.cluster.v3.OutlierDetection>` 是一样的，即：

outlier_detection.consecutive_5xx
  异常检测中的 :ref:`consecutive_5XX
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_5xx>` 设置

outlier_detection.consecutive_gateway_failure
  异常检测中的 :ref:`consecutive_gateway_failure
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_gateway_failure>` 设置

outlier_detection.consecutive_local_origin_failure
  异常检测中的 :ref:`consecutive_local_origin_failure
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_local_origin_failure>` 设置

outlier_detection.interval_ms
  异常检测中的 :ref:`interval_ms
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.interval>` 设置

outlier_detection.base_ejection_time_ms
  异常检测中的 :ref:`base_ejection_time_ms
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.base_ejection_time>` 设置

outlier_detection.max_ejection_percent
  异常检测中的 :ref:`max_ejection_percent
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.max_ejection_percent>` 设置

outlier_detection.enforcing_consecutive_5xx
  异常检测中的 :ref:`enforcing_consecutive_5xx
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_consecutive_5xx>` 设置

outlier_detection.enforcing_consecutive_gateway_failure
  异常检测中的 :ref:`enforcing_consecutive_gateway_failure
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_consecutive_gateway_failure>` 设置

outlier_detection.enforcing_consecutive_local_origin_failure
  异常检测中的 :ref:`enforcing_consecutive_local_origin_failure
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_consecutive_local_origin_failure>` 设置

outlier_detection.enforcing_success_rate
  异常检测中的 :ref:`enforcing_success_rate
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_success_rate>` 设置

outlier_detection.enforcing_local_origin_success_rate
  异常检测中的 :ref:`enforcing_local_origin_success_rate
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_local_origin_success_rate>` 设置

outlier_detection.success_rate_minimum_hosts
  异常检测中的 :ref:`success_rate_minimum_hosts
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_minimum_hosts>` 设置

outlier_detection.success_rate_request_volume
  异常检测中的 :ref:`success_rate_request_volume
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_request_volume>` 设置
  setting in outlier detection

outlier_detection.success_rate_stdev_factor
  异常检测中的 :ref:`success_rate_stdev_factor
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_stdev_factor>` 设置

outlier_detection.enforcing_failure_percentage
  异常检测中的 :ref:`enforcing_failure_percentage
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_failure_percentage>` 设置

outlier_detection.enforcing_failure_percentage_local_origin
  异常检测中的 :ref:`enforcing_failure_percentage_local_origin
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_failure_percentage_local_origin>` 设置

outlier_detection.failure_percentage_request_volume
  异常检测中的 :ref:`failure_percentage_request_volume
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.failure_percentage_request_volume>` 设置

outlier_detection.failure_percentage_minimum_hosts
  异常检测中的 :ref:`failure_percentage_minimum_hosts
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.failure_percentage_minimum_hosts>` 设置

outlier_detection.failure_percentage_threshold
  异常检测中的 :ref:`failure_percentage_threshold
  <envoy_v3_api_field_config.cluster.v3.OutlierDetection.failure_percentage_threshold>` 设置

核心
----

upstream.healthy_panic_threshold
  设置 :ref:`紧急模式 <arch_overview_load_balancing_panic_threshold>` 的百分比。
  默认值是 50%。

upstream.use_http2
  设置集群是否使用了  *http2* :ref:`protocol options <envoy_v3_api_field_config.cluster.v3.Cluster.http2_protocol_options>`，如果配置了该选项的话。设置为 0 来禁用 HTTP/2，即使配置了该特性。默认是开启的。

.. _config_cluster_manager_cluster_runtime_zone_routing:

区域感知负载均衡
------------------

upstream.zone_routing.enabled
  路由到相同上游区域的请求百分比。默认是 100%。

upstream.zone_routing.min_cluster_size
  尝试区域感知路由的上游集群的最小值。默认值是 6，如果上游集群的大小小于 *min_cluster_size*，则不会执行区域感知路由。

熔断
----------------

circuit_breakers.<cluster_name>.<priority>.max_connections
  :ref:`最大连接熔断器设置 <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_connections>`

circuit_breakers.<cluster_name>.<priority>.max_pending_requests
  :ref:`最大挂起请求熔断器设置 <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_pending_requests>`

circuit_breakers.<cluster_name>.<priority>.max_requests
  :ref:`最大请求熔断器设置 <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_requests>`

circuit_breakers.<cluster_name>.<priority>.max_retries
  :ref:`最大重试熔断器设置 <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_retries>`

circuit_breakers.<cluster_name>.<priority>.retry_budget.budget_percent
  :ref:`最大重试熔断器设置 <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.RetryBudget.budget_percent>`

circuit_breakers.<cluster_name>.<priority>.retry_budget.min_retry_concurrency
  :ref:`最大重试熔断器设置 <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.RetryBudget.min_retry_concurrency>`
