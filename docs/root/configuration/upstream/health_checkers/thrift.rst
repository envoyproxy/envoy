.. _config_health_checkers_thrift:

Thrift Health Checker
=====================

The Thrift Health Checker (with :code:`envoy.health_checkers.thrift` as name) uses Thrift requests,
responses and exceptions to check upstream hosts. It sends a request with
:ref:`method_name <envoy_v3_api_field_extensions.health_checkers.thrift.v3.Thrift.method_name>`
and expects a success response in exchange. The upstream host can also respond with an exception to cause the check to fail.
The :ref:`transport <envoy_v3_api_field_extensions.health_checkers.thrift.v3.Thrift.transport>` and
:ref:`protocol <envoy_v3_api_field_extensions.health_checkers.thrift.v3.Thrift.protocol>` types to be set for each health
check request must be configured to enable Thrift health checks. The sequence id is always 0 for each health check request.


An example for :ref:`custom_health_check <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>`
using the Thrift health checker is shown below:


.. code-block:: yaml

  custom_health_check:
    name: envoy.health_checkers.thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        transport: HEADER
        protocol: BINARY

* :ref:`v3 API reference <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>`
