.. _config_health_checkers_thrift:

Thrift Health Checker
=====================

The Thrift health checker is a custom health checker (with :code:`envoy.health_checkers.thrift` as name)
which checks Thrift upstream hosts. It sends a thrift request with
:ref:`method_name <envoy_v3_api_field_extensions.health_checkers.thrift.v3.Thrift.method_name>` and expect
a success response. The Thrift server can respond an exception to cause an immediate health check failure.
Must specify :ref:`transport <envoy_v3_api_field_extensions.health_checkers.thrift.v3.Thrift.transport>` and
:ref:`protocol <envoy_v3_api_field_extensions.health_checkers.thrift.v3.Thrift.protocol>` for health checker
to generate the expected thrift request.

An example setting for :ref:`custom_health_check <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>` as a
Thrift health checker is shown below:

.. code-block:: yaml

  custom_health_check:
    name: envoy.health_checkers.thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        transport: HEADER
        protocol: BINARY

* :ref:`v3 API reference <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>`
