.. _config_http_filters_health_check:

Health check
============

* Health check filter :ref:`architecture overview <arch_overview_health_checking_filter>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.health_check.v2.HealthCheck>`

.. note::

  Note that the filter will automatically fail health checks and set the
  :ref:`x-envoy-immediate-health-check-fail
  <config_http_filters_router_x-envoy-immediate-health-check-fail>` header if the
  :ref:`/healthcheck/fail <operations_admin_interface_healthcheck_fail>` admin endpoint has been
  called. (The :ref:`/healthcheck/ok <operations_admin_interface_healthcheck_ok>` admin endpoint
  reverses this behavior).
