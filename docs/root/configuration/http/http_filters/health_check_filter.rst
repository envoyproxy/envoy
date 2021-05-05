.. _config_http_filters_health_check:

Health check
============

* Health check filter :ref:`architecture overview <arch_overview_health_checking_filter>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.health_check.v3.HealthCheck>`
* This filter should be configured with the name *envoy.filters.http.health_check*.

.. note::

  Note that the filter will automatically fail health checks and set the
  :ref:`x-envoy-immediate-health-check-fail
  <config_http_filters_router_x-envoy-immediate-health-check-fail>` header on all responses (both
  health check and normal requests) if the :ref:`/healthcheck/fail
  <operations_admin_interface_healthcheck_fail>` admin endpoint has been called. (The
  :ref:`/healthcheck/ok <operations_admin_interface_healthcheck_ok>` admin endpoint reverses this
  behavior).
