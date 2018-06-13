.. _config_cluster_manager_cluster_stats:

Statistics
==========

.. contents::
  :local:

General
-------

The cluster manager has a statistics tree rooted at *cluster_manager.* with the following
statistics. Any ``:`` character in the stats name is replaced with ``_``.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  cluster_added, Counter, Total clusters added (either via static config or CDS)
  cluster_modified, Counter, Total clusters modified (via CDS)
  cluster_removed, Counter, Total clusters removed (via CDS)
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
  upstream_cx_protocol_error, Counter, Total connection protocol errors
  upstream_cx_max_requests, Counter, Total connections closed due to maximum requests
  upstream_cx_none_healthy, Counter, Total times connection not established due to no healthy hosts
  upstream_rq_total, Counter, Total requests
  upstream_rq_active, Gauge, Total active requests
  upstream_rq_pending_total, Counter, Total requests pending a connection pool connection
  upstream_rq_pending_overflow, Counter, Total requests that overflowed connection pool circuit breaking and were failed
  upstream_rq_pending_failure_eject, Counter, Total requests that were failed due to a connection pool connection failure
  upstream_rq_pending_active, Gauge, Total active requests pending a connection pool connection
  upstream_rq_cancelled, Counter, Total requests cancelled before obtaining a connection pool connection
  upstream_rq_maintenance_mode, Counter, Total requests that resulted in an immediate 503 due to :ref:`maintenance mode<config_http_filters_router_runtime_maintenance_mode>`
  upstream_rq_timeout, Counter, Total requests that timed out waiting for a response
  upstream_rq_per_try_timeout, Counter, Total requests that hit the per try timeout
  upstream_rq_rx_reset, Counter, Total requests that were reset remotely
  upstream_rq_tx_reset, Counter, Total requests that were reset locally
  upstream_rq_retry, Counter, Total request retries
  upstream_rq_retry_success, Counter, Total request retry successes
  upstream_rq_retry_overflow, Counter, Total requests not retried due to circuit breaking
  upstream_flow_control_paused_reading_total, Counter, Total number of times flow control paused reading from upstream
  upstream_flow_control_resumed_reading_total, Counter, Total number of times flow control resumed reading from upstream
  upstream_flow_control_backed_up_total, Counter, Total number of times the upstream connection backed up and paused reads from downstream
  upstream_flow_control_drained_total, Counter, Total number of times the upstream connection drained and resumed reads from downstream
  membership_change, Counter, Total cluster membership changes
  membership_healthy, Gauge, Current cluster healthy total (inclusive of both health checking and outlier detection)
  membership_total, Gauge, Current cluster membership total
  retry_or_shadow_abandoned, Counter, Total number of times shadowing or retry buffering was canceled due to buffer limits
  config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
  update_attempt, Counter, Total cluster membership update attempts
  update_success, Counter, Total cluster membership update successes
  update_failure, Counter, Total cluster membership update failures
  update_empty, Counter, Total cluster membership updates ending with empty cluster load assignment and continuing with previous config
  update_no_rebuild, Counter, Total successful cluster membership updates that didn't result in any cluster load balancing structure rebuilds
  version, Gauge, Hash of the contents from the last successful API fetch
  max_host_weight, Gauge, Maximum weight of any host in the cluster
  bind_errors, Counter, Total errors binding the socket to the configured source address

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
  ejections_enforced_success_rate, Counter, Number of enforced success rate outlier ejections
  ejections_detected_success_rate, Counter, Number of detected success rate outlier ejections (even if unenforced)
  ejections_enforced_consecutive_gateway_failure, Counter, Number of enforced consecutive gateway failure ejections
  ejections_detected_consecutive_gateway_failure, Counter, Number of detected consecutive gateway failure ejections (even if unenforced)
  ejections_total, Counter, Deprecated. Number of ejections due to any outlier type (even if unenforced)
  ejections_consecutive_5xx, Counter, Deprecated. Number of consecutive 5xx ejections (even if unenforced)

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

  upstream_rq_<\*xx>, Counter, "Aggregate HTTP response codes (e.g., 2xx, 3xx, etc.)"
  upstream_rq_<\*>, Counter, "Specific HTTP response codes (e.g., 201, 302, etc.)"
  upstream_rq_time, Histogram, Request time milliseconds
  canary.upstream_rq_<\*xx>, Counter, Upstream canary aggregate HTTP response codes
  canary.upstream_rq_<\*>, Counter, Upstream canary specific HTTP response codes
  canary.upstream_rq_time, Histogram, Upstream canary request time milliseconds
  internal.upstream_rq_<\*xx>, Counter, Internal origin aggregate HTTP response codes
  internal.upstream_rq_<\*>, Counter, Internal origin specific HTTP response codes
  internal.upstream_rq_time, Histogram, Internal origin request time milliseconds
  external.upstream_rq_<\*xx>, Counter, External origin aggregate HTTP response codes
  external.upstream_rq_<\*>, Counter, External origin specific HTTP response codes
  external.upstream_rq_time, Histogram, External origin request time milliseconds

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
and the :ref:`upstream cluster <arch_overview_service_discovery_types_sds>`,
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

Load balancer subset statistics
-------------------------------

Statistics for monitoring `load balancer subset <arch_overview_load_balancer_subsets>`
decisions. Stats are rooted at *cluster.<name>.* and contain the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  lb_subsets_active, Gauge, Number of currently available subsets
  lb_subsets_created, Counter, Number of subsets created
  lb_subsets_removed, Counter, Number of subsets removed due to no hosts
  lb_subsets_selected, Counter, Number of times any subset was selected for load balancing
  lb_subsets_fallback, Counter, Number of times the fallback policy was invoked
