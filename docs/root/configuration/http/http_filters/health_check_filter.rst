.. _config_http_filters_health_check:

Health check
============

* Health check filter :ref:`architecture overview <arch_overview_health_checking_filter>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.health_check.v3.HealthCheck>`
* This filter should be configured with the name *envoy.filters.http.health_check*.

.. note::

  Note that the filter will automatically fail health checks and set the
  :ref:`x-envoy-immediate-health-check-fail
  <config_http_filters_router_x-envoy-immediate-health-check-fail>` header if the
  :ref:`/healthcheck/fail <operations_admin_interface_healthcheck_fail>` admin endpoint has been
  called. (The :ref:`/healthcheck/ok <operations_admin_interface_healthcheck_ok>` admin endpoint
  reverses this behavior).


健康检查
============

* 健康检查过滤器 :ref:`架构概述 <arch_overview_health_checking_filter>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.health_check.v3.HealthCheck>`
* 此过滤器通过参数 *envoy.filters.http.health_check* 来配置。

.. note::

  请注意如果调用了管理端点:ref:`/healthcheck/fail <operations_admin_interface_healthcheck_fail>`，过滤器将会自动
  地在健康检查中失败并标示:ref:`x-envoy-immediate-health-check-fail <config_http_filters_router_x-envoy-immediate-health-check-fail>`标头。（:ref:`/healthcheck/ok <operations_admin_interface_healthcheck_ok>` 管理端点可以逆反此行为）。