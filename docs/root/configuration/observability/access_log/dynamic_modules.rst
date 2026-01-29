.. _config_access_log_dynamic_modules:

Dynamic Modules Access Logger
=============================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.access_loggers.dynamic_modules.v3.DynamicModuleAccessLog>`

The Dynamic Modules Access Logger allows you to write access loggers in a dynamic module.
This can be used to implement custom logging behavior, such as:

*   Sending logs to custom backends.
*   Custom log formatting and aggregation.
*   Real-time metrics extraction from access logs.
*   Integration with proprietary logging systems.

The logger receives completed request information including:

*   Request and response headers (and trailers).
*   Response code and details.
*   Response flags (indicating errors, timeouts, etc.).
*   Timing information (request duration, upstream latency, etc.).
*   Byte counts (request/response sizes).
*   Upstream information (cluster, host, connection details).
*   TLS information (versions, certificates).
*   Tracing information (trace ID, span ID).
*   Dynamic metadata and filter state.

Example Configuration
---------------------

.. code-block:: yaml

  access_log:
  - name: envoy.access_loggers.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.dynamic_modules.v3.DynamicModuleAccessLog
      dynamic_module_config:
        name: my_logger
      logger_name: json_logger
      logger_config:
        "@type": type.googleapis.com/google.protobuf.Struct
        value:
          output_path: "/var/log/envoy/access.json"
          buffer_size: 100

For more details on dynamic modules, see the :ref:`architecture overview <arch_overview_dynamic_modules>`.

