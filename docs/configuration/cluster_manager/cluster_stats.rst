.. _config_cluster_manager_cluster_stats:

Statistics
==========

Every cluster has a statistics tree rooted at *cluster.<name>.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_cx_total, Counter, Description
  upstream_cx_active, Gauge, Description
  upstream_cx_http1_total, Counter, Description
  upstream_cx_http2_total, Counter, Description
  upstream_cx_connect_fail, Counter, Description
  upstream_cx_connect_timeout, Counter, Description
  upstream_cx_connect_ms, Timer, Description
  upstream_cx_length_ms, Timer, Description
  upstream_cx_destroy, Counter, Description
  upstream_cx_destroy_local, Counter, Description
  upstream_cx_destroy_remote, Counter, Description
  upstream_cx_destroy_with_active_rq, Counter, Description
  upstream_cx_destroy_local_with_active_rq, Counter, Description
  upstream_cx_destroy_remote_with_active_rq, Counter, Description
  upstream_cx_close_header, Counter, Description
  upstream_cx_rx_bytes_total, Counter, Description
  upstream_cx_rx_bytes_buffered, Gauge, Description
  upstream_cx_tx_bytes_total, Counter, Description
  upstream_cx_tx_bytes_buffered, Gauge, Description
  upstream_cx_protocol_error, Counter, Description
  upstream_cx_max_requests, Counter, Description
  upstream_cx_none_healthy, Counter, Description
  upstream_rq_total, Counter, Description
  upstream_rq_active, Gauge, Description
  upstream_rq_pending_total, Counter, Description
  upstream_rq_pending_overflow, Counter, Description
  upstream_rq_pending_failure_eject, Counter, Description
  upstream_rq_pending_active, Gauge, Description
  upstream_rq_cancelled, Counter, Description
  upstream_rq_timeout, Counter, Description
  upstream_rq_rx_reset, Counter, Description
  upstream_rq_tx_reset, Counter, Description
  upstream_rq_retry, Counter, Description
  upstream_rq_retry_success, Counter, Description
  upstream_rq_retry_overflow, Counter, Description
  upstream_rq_lb_healthy_panic, Counter, Description
  membership_change, Counter, Description
  membership_total, Gauge, Description
  update_attempt, Counter, Description
  update_success, Counter, Description
  update_failure, Counter, Description
  max_host_weight, Gauge, Description

Health check statistics
-----------------------

If health check is configured, the cluster has an additional statistics tree rooted at
*cluster.<name>.health_check.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  attempt, Counter, Total number of health checks
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
<config_http_filters_router>` and :ref:`rate limit filter <config_http_filters_rate_limit>`.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  stat1, Counter, Description

.. _config_cluster_manager_cluster_stats_alt_tree:

Alternate tree dynamic HTTP statistics
--------------------------------------

FIXFIX
