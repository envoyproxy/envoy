.. _config_cluster_manager_cluster_stats:

Statistics
==========

.. contents::
  :local:

General
-------

Every cluster has a statistics tree rooted at *cluster.<name>.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_cx_total, Counter, Total connections
  upstream_cx_active, Gauge, Total active connections
  upstream_cx_http1_total, Counter, Total HTTP/1.1 connections
  upstream_cx_http2_total, Counter, Total HTTP/2 connections
  upstream_cx_connect_fail, Counter, Total connection failures
  upstream_cx_connect_timeout, Counter, Total connection timeouts
  upstream_cx_overflow, Counter, Total times that the cluster's connection circuit breaker overflowed
  upstream_cx_connect_ms, Timer, Connection establishment milliseconds
  upstream_cx_length_ms, Timer, Connection length milliseconds
  upstream_cx_destroy, Counter, Total destroyed connections
  upstream_cx_destroy_local, Counter, Total connections destroyed locally
  upstream_cx_destroy_remote, Counter, Total connections destroyed remotely
  upstream_cx_destroy_with_active_rq, Counter, Total connections destroyed with 1+ active request
  upstream_cx_destroy_local_with_active_rq, Counter, Total connections destroyed locally with 1+ active request
  upstream_cx_destroy_remote_with_active_rq, Counter, Total connections destroyed remotely with 1+ active request
  upstream_cx_close_header, Counter, Total connections closed via HTTP/1.1 connection close header
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
  upstream_rq_timeout, Counter, Total requests that timed out waiting for a response
  upstream_rq_per_try_timeout, Counter, Total requests that hit the per try timeout
  upstream_rq_rx_reset, Counter, Total requests that were reset remotely
  upstream_rq_tx_reset, Counter, Total requests that were reset locally
  upstream_rq_retry, Counter, Total request retries
  upstream_rq_retry_success, Counter, Total request retry successes
  upstream_rq_retry_overflow, Counter, Total requests not retried due to circuit breaking
  membership_change, Counter, Total cluster membership changes
  membership_healthy, Gauge, Current cluster healthy total (inclusive of both health checking and outlier detection)
  membership_total, Gauge, Current cluster membership total
  update_attempt, Counter, Total cluster membership update attempts
  update_success, Counter, Total cluster membership update successes
  update_failure, Counter, Total cluster membership update failures
  max_host_weight, Gauge, Maximum weight of any host in the cluster

Health check statistics
-----------------------

If health check is configured, the cluster has an additional statistics tree rooted at
*cluster.<name>.health_check.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  attempt, Counter, Number of health checks
  success, Counter, Number of successful health checks
  failure, Counter, Number of failed health checks
  timeout, Counter, Number of timed out health checks
  protocol_error, Counter, Number of protocol errors
  verify_cluster, Counter, Number of health checks that attempted cluster name verification
  healthy, Gauge, Number of healthy members

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
  upstream_rq_time, Timer, Request time milliseconds
  canary.upstream_rq_<\*xx>, Counter, Upstream canary aggregate HTTP response codes
  canary.upstream_rq_<\*>, Counter, Upstream canary specific HTTP response codes
  canary.upstream_rq_time, Timer, Upstream canary request time milliseconds
  internal.upstream_rq_<\*xx>, Counter, Internal origin aggregate HTTP response codes
  internal.upstream_rq_<\*>, Counter, Internal origin specific HTTP response codes
  internal.upstream_rq_time, Timer, Internal origin request time milliseconds
  external.upstream_rq_<\*xx>, Counter, External origin aggregate HTTP response codes
  external.upstream_rq_<\*>, Counter, External origin specific HTTP response codes
  external.upstream_rq_time, Timer, External origin request time milliseconds

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
and the :ref:`upstream cluster <arch_overview_service_discovery_sds>`,
Envoy will track the following statistics in *cluster.<name>.zone.<from_zone>.<to_zone>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_rq_<\*xx>, Counter, "Aggregate HTTP response codes (e.g., 2xx, 3xx, etc.)"
  upstream_rq_<\*>, Counter, "Specific HTTP response codes (e.g., 201, 302, etc.)"
  upstream_rq_time, Timer, Request time milliseconds

Load balancer statistics
------------------------

Statistics for monitoring load balancer decisions. Stats are rooted at *cluster.<name>.* and contain
the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  lb_healthy_panic, Counter, Total requests load balanced with the load balancer in panic mode
  lb_zone_cluster_too_small, Counter, No zone aware routing because of small upstream cluster size
  lb_zone_routing_all_directly, Counter, Sending all requests directly to the same zone
  lb_zone_routing_sampled, Counter, Sending some requests to the same zone
  lb_zone_routing_cross_zone, Counter, Zone aware routing mode but have to send cross zone
  lb_local_cluster_not_ok, Counter, Local host set is not set or it is panic mode for local cluster
  lb_zone_number_differs, Counter, Number of zones in local and upstream cluster different
