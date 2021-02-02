.. _config_cluster_manager_cluster_stats:

Statistics
==========

.. contents::
  :local:

General
-------

The cluster manager has a statistics tree rooted at *cluster_manager.* with the following
statistics. Any ``:`` character in the stats name is replaced with ``_``. Stats include
all clusters managed by the cluster manager, including both clusters used for data plane
upstreams and control plane xDS clusters.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  cluster_added, Counter, Total clusters added (either via static config or CDS)
  cluster_modified, Counter, Total clusters modified (via CDS)
  cluster_removed, Counter, Total clusters removed (via CDS)
  cluster_updated, Counter, Total cluster updates
  cluster_updated_via_merge, Counter, Total cluster updates applied as merged updates
  update_merge_cancelled, Counter, Total merged updates that got cancelled and delivered early
  update_out_of_merge_window, Counter, Total updates which arrived out of a merge window
  active_clusters, Gauge, Number of currently active (warmed) clusters
  warming_clusters, Gauge, Number of currently warming (not active) clusters

Every cluster has a statistics tree rooted at *cluster.<name>.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_cx_total, Counter, Total connections
  upstream_cx_active, Gauge, Total active connections
  upstream_cx_http1_total, Counter, Total HTTP/1.1 connections
  upstream_cx_http2_total, Counter, Total HTTP/2 connections
  upstream_cx_connect_fail, Counter, Total connection failures
  upstream_cx_connect_timeout, Counter, Total connection connect timeouts
  upstream_cx_idle_timeout, Counter, Total connection idle timeouts
  upstream_cx_connect_attempts_exceeded, Counter, Total consecutive connection failures exceeding configured connection attempts
  upstream_cx_overflow, Counter, Total times that the cluster's connection circuit breaker overflowed
  upstream_cx_connect_ms, Histogram, Connection establishment milliseconds
  upstream_cx_length_ms, Histogram, Connection length milliseconds
  upstream_cx_destroy, Counter, Total destroyed connections
  upstream_cx_destroy_local, Counter, Total connections destroyed locally
  upstream_cx_destroy_remote, Counter, Total connections destroyed remotely
  upstream_cx_destroy_with_active_rq, Counter, Total connections destroyed with 1+ active request
  upstream_cx_destroy_local_with_active_rq, Counter, Total connections destroyed locally with 1+ active request
  upstream_cx_destroy_remote_with_active_rq, Counter, Total connections destroyed remotely with 1+ active request
  upstream_cx_close_notify, Counter, Total connections closed via HTTP/1.1 connection close header or HTTP/2 GOAWAY
  upstream_cx_rx_bytes_total, Counter, Total received connection bytes
  upstream_cx_rx_bytes_buffered, Gauge, Received connection bytes currently buffered
  upstream_cx_tx_bytes_total, Counter, Total sent connection bytes
  upstream_cx_tx_bytes_buffered, Gauge, Send connection bytes currently buffered
  upstream_cx_pool_overflow, Counter, Total times that the cluster's connection pool circuit breaker overflowed
  upstream_cx_protocol_error, Counter, Total connection protocol errors
  upstream_cx_max_requests, Counter, Total connections closed due to maximum requests
  upstream_cx_none_healthy, Counter, Total times connection not established due to no healthy hosts
  upstream_rq_total, Counter, Total requests
  upstream_rq_active, Gauge, Total active requests
  upstream_rq_pending_total, Counter, Total requests pending a connection pool connection
  upstream_rq_pending_overflow, Counter, Total requests that overflowed connection pool or requests (mainly for HTTP/2) circuit breaking and were failed
  upstream_rq_pending_failure_eject, Counter, Total requests that were failed due to a connection pool connection failure or remote connection termination
  upstream_rq_pending_active, Gauge, Total active requests pending a connection pool connection
  upstream_rq_cancelled, Counter, Total requests cancelled before obtaining a connection pool connection
  upstream_rq_maintenance_mode, Counter, Total requests that resulted in an immediate 503 due to :ref:`maintenance mode<config_http_filters_router_runtime_maintenance_mode>`
  upstream_rq_timeout, Counter, Total requests that timed out waiting for a response
  upstream_rq_max_duration_reached, Counter, Total requests closed due to max duration reached
  upstream_rq_per_try_timeout, Counter, Total requests that hit the per try timeout (except when request hedging is enabled)
  upstream_rq_rx_reset, Counter, Total requests that were reset remotely
  upstream_rq_tx_reset, Counter, Total requests that were reset locally
  upstream_rq_retry, Counter, Total request retries
  upstream_rq_retry_backoff_exponential, Counter, Total retries using the exponential backoff strategy
  upstream_rq_retry_backoff_ratelimited, Counter, Total retries using the ratelimited backoff strategy
  upstream_rq_retry_limit_exceeded, Counter, Total requests not retried due to exceeding :ref:`the configured number of maximum retries <config_http_filters_router_x-envoy-max-retries>`
  upstream_rq_retry_success, Counter, Total request retry successes
  upstream_rq_retry_overflow, Counter, Total requests not retried due to circuit breaking or exceeding the :ref:`retry budget <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.retry_budget>`
  upstream_flow_control_paused_reading_total, Counter, Total number of times flow control paused reading from upstream
  upstream_flow_control_resumed_reading_total, Counter, Total number of times flow control resumed reading from upstream
  upstream_flow_control_backed_up_total, Counter, Total number of times the upstream connection backed up and paused reads from downstream
  upstream_flow_control_drained_total, Counter, Total number of times the upstream connection drained and resumed reads from downstream
  upstream_internal_redirect_failed_total, Counter, Total number of times failed internal redirects resulted in redirects being passed downstream.
  upstream_internal_redirect_succeed_total, Counter, Total number of times internal redirects resulted in a second upstream request.
  membership_change, Counter, Total cluster membership changes
  membership_healthy, Gauge, Current cluster healthy total (inclusive of both health checking and outlier detection)
  membership_degraded, Gauge, Current cluster :ref:`degraded <arch_overview_load_balancing_degraded>` total
  membership_excluded, Gauge, Current cluster :ref:`excluded <arch_overview_load_balancing_excluded>` total
  membership_total, Gauge, Current cluster membership total
  retry_or_shadow_abandoned, Counter, Total number of times shadowing or retry buffering was canceled due to buffer limits
  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  update_attempt, Counter, Total attempted cluster membership updates by service discovery
  update_success, Counter, Total successful cluster membership updates by service discovery
  update_failure, Counter, Total failed cluster membership updates by service discovery
  update_duration, Histogram, Amount of time spent updating configs
  update_empty, Counter, Total cluster membership updates ending with empty cluster load assignment and continuing with previous config
  update_no_rebuild, Counter, Total successful cluster membership updates that didn't result in any cluster load balancing structure rebuilds
  version, Gauge, Hash of the contents from the last successful API fetch
  max_host_weight, Gauge, Maximum weight of any host in the cluster
  bind_errors, Counter, Total errors binding the socket to the configured source address
  assignment_timeout_received, Counter, Total assignments received with endpoint lease information.
  assignment_stale, Counter, Number of times the received assignments went stale before new assignments arrived.

Health check statistics
-----------------------

If health check is configured, the cluster has an additional statistics tree rooted at
*cluster.<name>.health_check.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  attempt, Counter, Number of health checks
  success, Counter, Number of successful health checks
  failure, Counter, Number of immediately failed health checks (e.g. HTTP 503) as well as network failures
  passive_failure, Counter, Number of health check failures due to passive events (e.g. x-envoy-immediate-health-check-fail)
  network_failure, Counter, Number of health check failures due to network error
  verify_cluster, Counter, Number of health checks that attempted cluster name verification
  healthy, Gauge, Number of healthy members

.. _config_cluster_manager_cluster_stats_outlier_detection:

Outlier detection statistics
----------------------------

If :ref:`outlier detection <arch_overview_outlier_detection>` is configured for a cluster,
statistics will be rooted at *cluster.<name>.outlier_detection.* and contain the following:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ejections_enforced_total, Counter, Number of enforced ejections due to any outlier type
  ejections_active, Gauge, Number of currently ejected hosts
  ejections_overflow, Counter, Number of ejections aborted due to the max ejection %
  ejections_enforced_consecutive_5xx, Counter, Number of enforced consecutive 5xx ejections
  ejections_detected_consecutive_5xx, Counter, Number of detected consecutive 5xx ejections (even if unenforced)
  ejections_enforced_success_rate, Counter, Number of enforced success rate outlier ejections. Exact meaning of this counter depends on :ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` config item. Refer to :ref:`Outlier Detection documentation<arch_overview_outlier_detection>` for details.
  ejections_detected_success_rate, Counter, Number of detected success rate outlier ejections (even if unenforced). Exact meaning of this counter depends on :ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` config item. Refer to :ref:`Outlier Detection documentation<arch_overview_outlier_detection>` for details.
  ejections_enforced_consecutive_gateway_failure, Counter, Number of enforced consecutive gateway failure ejections
  ejections_detected_consecutive_gateway_failure, Counter, Number of detected consecutive gateway failure ejections (even if unenforced)
  ejections_enforced_consecutive_local_origin_failure, Counter, Number of enforced consecutive local origin failure ejections
  ejections_detected_consecutive_local_origin_failure, Counter, Number of detected consecutive local origin failure ejections (even if unenforced)
  ejections_enforced_local_origin_success_rate, Counter, Number of enforced success rate outlier ejections for locally originated failures
  ejections_detected_local_origin_success_rate, Counter, Number of detected success rate outlier ejections for locally originated failures (even if unenforced)
  ejections_enforced_failure_percentage, Counter, Number of enforced failure percentage outlier ejections. Exact meaning of this counter depends on :ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` config item. Refer to :ref:`Outlier Detection documentation<arch_overview_outlier_detection>` for details.
  ejections_detected_failure_percentage, Counter, Number of detected failure percentage outlier ejections (even if unenforced). Exact meaning of this counter depends on :ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` config item. Refer to :ref:`Outlier Detection documentation<arch_overview_outlier_detection>` for details.
  ejections_enforced_failure_percentage_local_origin, Counter, Number of enforced failure percentage outlier ejections for locally originated failures
  ejections_detected_failure_percentage_local_origin, Counter, Number of detected failure percentage outlier ejections for locally originated failures (even if unenforced)
  ejections_total, Counter, Deprecated. Number of ejections due to any outlier type (even if unenforced)
  ejections_consecutive_5xx, Counter, Deprecated. Number of consecutive 5xx ejections (even if unenforced)

.. _config_cluster_manager_cluster_stats_circuit_breakers:

Circuit breakers statistics
---------------------------

Circuit breakers statistics will be rooted at *cluster.<name>.circuit_breakers.<priority>.* and contain the following:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  cx_open, Gauge, Whether the connection circuit breaker is closed (0) or open (1)
  cx_pool_open, Gauge, Whether the connection pool circuit breaker is closed (0) or open (1)
  rq_pending_open, Gauge, Whether the pending requests circuit breaker is closed (0) or open (1)
  rq_open, Gauge, Whether the requests circuit breaker is closed (0) or open (1)
  rq_retry_open, Gauge, Whether the retry circuit breaker is closed (0) or open (1)
  remaining_cx, Gauge, Number of remaining connections until the circuit breaker opens
  remaining_pending, Gauge, Number of remaining pending requests until the circuit breaker opens
  remaining_rq, Gauge, Number of remaining requests until the circuit breaker opens
  remaining_retries, Gauge, Number of remaining retries until the circuit breaker opens

.. _config_cluster_manager_cluster_stats_timeout_budgets:

Timeout budget statistics
-------------------------

If :ref:`timeout budget statistic tracking <envoy_v3_api_field_config.cluster.v3.Cluster.track_timeout_budgets>` is
turned on, statistics will be added to *cluster.<name>* and contain the following:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   upstream_rq_timeout_budget_percent_used, Histogram, What percentage of the global timeout was used waiting for a response
   upstream_rq_timeout_budget_per_try_percent_used, Histogram, What percentage of the per try timeout was used waiting for a response

.. _config_cluster_manager_cluster_stats_dynamic_http:

Dynamic HTTP statistics
-----------------------

If HTTP is used, dynamic HTTP response code statistics are also available. These are emitted by
various internal systems as well as some filters such as the :ref:`router filter
<config_http_filters_router>` and :ref:`rate limit filter <config_http_filters_rate_limit>`. They
are rooted at *cluster.<name>.* and contain the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_rq_completed, Counter, "Total upstream requests completed"
  upstream_rq_<\*xx>, Counter, "Aggregate HTTP response codes (e.g., 2xx, 3xx, etc.)"
  upstream_rq_<\*>, Counter, "Specific HTTP response codes (e.g., 201, 302, etc.)"
  upstream_rq_time, Histogram, Request time milliseconds
  canary.upstream_rq_completed, Counter, "Total upstream canary requests completed"
  canary.upstream_rq_<\*xx>, Counter, Upstream canary aggregate HTTP response codes
  canary.upstream_rq_<\*>, Counter, Upstream canary specific HTTP response codes
  canary.upstream_rq_time, Histogram, Upstream canary request time milliseconds
  internal.upstream_rq_completed, Counter, "Total internal origin requests completed"
  internal.upstream_rq_<\*xx>, Counter, Internal origin aggregate HTTP response codes
  internal.upstream_rq_<\*>, Counter, Internal origin specific HTTP response codes
  internal.upstream_rq_time, Histogram, Internal origin request time milliseconds
  external.upstream_rq_completed, Counter, "Total external origin requests completed"
  external.upstream_rq_<\*xx>, Counter, External origin aggregate HTTP response codes
  external.upstream_rq_<\*>, Counter, External origin specific HTTP response codes
  external.upstream_rq_time, Histogram, External origin request time milliseconds

.. _config_cluster_manager_cluster_stats_tls:

TLS statistics
--------------

If TLS is used by the cluster the following statistics are rooted at *cluster.<name>.ssl.*:

.. include:: ../../../_include/ssl_stats.rst

.. _config_cluster_manager_cluster_stats_alt_tree:

Alternate tree dynamic HTTP statistics
--------------------------------------

If alternate tree statistics are configured, they will be present in the
*cluster.<name>.<alt name>.* namespace. The statistics produced are the same as documented in
the dynamic HTTP statistics section :ref:`above
<config_cluster_manager_cluster_stats_dynamic_http>`.

.. _config_cluster_manager_cluster_per_az_stats:

Per service zone dynamic HTTP statistics
----------------------------------------

If the service zone is available for the local service (via :option:`--service-zone`)
and the :ref:`upstream cluster <arch_overview_service_discovery_types_eds>`,
Envoy will track the following statistics in *cluster.<name>.zone.<from_zone>.<to_zone>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_rq_<\*xx>, Counter, "Aggregate HTTP response codes (e.g., 2xx, 3xx, etc.)"
  upstream_rq_<\*>, Counter, "Specific HTTP response codes (e.g., 201, 302, etc.)"
  upstream_rq_time, Histogram, Request time milliseconds

Load balancer statistics
------------------------

Statistics for monitoring load balancer decisions. Stats are rooted at *cluster.<name>.* and contain
the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  lb_recalculate_zone_structures, Counter, The number of times locality aware routing structures are regenerated for fast decisions on upstream locality selection
  lb_healthy_panic, Counter, Total requests load balanced with the load balancer in panic mode
  lb_zone_cluster_too_small, Counter, No zone aware routing because of small upstream cluster size
  lb_zone_routing_all_directly, Counter, Sending all requests directly to the same zone
  lb_zone_routing_sampled, Counter, Sending some requests to the same zone
  lb_zone_routing_cross_zone, Counter, Zone aware routing mode but have to send cross zone
  lb_local_cluster_not_ok, Counter, Local host set is not set or it is panic mode for local cluster
  lb_zone_number_differs, Counter, Number of zones in local and upstream cluster different
  lb_zone_no_capacity_left, Counter, Total number of times ended with random zone selection due to rounding error
  original_dst_host_invalid, Counter, Total number of invalid hosts passed to original destination load balancer

.. _config_cluster_manager_cluster_stats_subset_lb:

Load balancer subset statistics
-------------------------------

Statistics for monitoring :ref:`load balancer subset <arch_overview_load_balancer_subsets>`
decisions. Stats are rooted at *cluster.<name>.* and contain the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  lb_subsets_active, Gauge, Number of currently available subsets
  lb_subsets_created, Counter, Number of subsets created
  lb_subsets_removed, Counter, Number of subsets removed due to no hosts
  lb_subsets_selected, Counter, Number of times any subset was selected for load balancing
  lb_subsets_fallback, Counter, Number of times the fallback policy was invoked
  lb_subsets_fallback_panic, Counter, Number of times the subset panic mode triggered
  lb_subsets_single_host_per_subset_duplicate, Gauge, Number of duplicate (unused) hosts when using :ref:`single_host_per_subset <envoy_v3_api_field_config.cluster.v3.Cluster.LbSubsetConfig.LbSubsetSelector.single_host_per_subset>`

.. _config_cluster_manager_cluster_stats_ring_hash_lb:

Ring hash load balancer statistics
----------------------------------

Statistics for monitoring the size and effective distribution of hashes when using the
:ref:`ring hash load balancer <arch_overview_load_balancing_types_ring_hash>`. Stats are rooted at
*cluster.<name>.ring_hash_lb.* and contain the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  size, Gauge, Total number of host hashes on the ring
  min_hashes_per_host, Gauge, Minimum number of hashes for a single host
  max_hashes_per_host, Gauge, Maximum number of hashes for a single host

.. _config_cluster_manager_cluster_stats_maglev_lb:

Maglev load balancer statistics
-------------------------------

Statistics for monitoring effective host weights when using the
:ref:`Maglev load balancer <arch_overview_load_balancing_types_maglev>`. Stats are rooted at
*cluster.<name>.maglev_lb.* and contain the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  min_entries_per_host, Gauge, Minimum number of entries for a single host
  max_entries_per_host, Gauge, Maximum number of entries for a single host

.. _config_cluster_manager_cluster_stats_request_response_sizes:

Request Response Size statistics
--------------------------------

If :ref:`request response size statistics <envoy_v3_api_field_config.cluster.v3.Cluster.track_cluster_stats>` are tracked,
statistics will be added to *cluster.<name>* and contain the following:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   upstream_rq_headers_size, Histogram, Request headers size in bytes per upstream
   upstream_rq_body_size, Histogram, Request body size in bytes per upstream
   upstream_rs_headers_size, Histogram, Response headers size in bytes per upstream
   upstream_rs_body_size, Histogram, Response body size in bytes per upstream
