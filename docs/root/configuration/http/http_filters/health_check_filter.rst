.. _config_http_filters_health_check:

Health check
============

* Health check filter :ref:`architecture overview <arch_overview_health_checking_filter>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.health_check.v3.HealthCheck>`

.. note::

  Note that the filter will automatically fail health checks and set the
  :ref:`x-envoy-immediate-health-check-fail
  <config_http_filters_router_x-envoy-immediate-health-check-fail>` header on all responses (both
  health check and normal requests) if the :ref:`/healthcheck/fail
  <operations_admin_interface_healthcheck_fail>` admin endpoint has been called. (The
  :ref:`/healthcheck/ok <operations_admin_interface_healthcheck_ok>` admin endpoint reverses this
  behavior).

Statistics
----------

The health check filter outputs statistics in the ``http.<stat_prefix>.health_check.`` namespace. The
:ref:`stat prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  request_total, Counter, Total number of requests processed by this health check filter ()including responses served from the cache)
  failed, Counter, Total number of health checks that failed (including failures due to cluster status and responses served from the cache)
  ok, Counter, Total number of health checks that passed
  cached_response, Counter, Total number of requests that were responded to with cached health check status
  failed_cluster_not_found, Counter, Total number of failed health checks due to referenced cluster not being found
  failed_cluster_empty, Counter, Total number of failed health checks due to empty cluster membership when checking cluster health
  failed_cluster_unhealthy, Counter, Total number of failed health checks due to cluster falling below minimum healthy percentage threshold
  degraded, Counter, Total number of health check responses that reported degraded status
