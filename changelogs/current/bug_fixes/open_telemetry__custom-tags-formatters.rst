Fixed the OpenTelemetry access loggers (both the gRPC and HTTP variants) ignoring configured
:ref:`formatters <envoy_v3_api_field_extensions.access_loggers.open_telemetry.v3.OpenTelemetryAccessLogConfig.formatters>`
when building :ref:`custom_tags <envoy_v3_api_field_extensions.access_loggers.open_telemetry.v3.OpenTelemetryAccessLogConfig.custom_tags>`.
Previously a custom tag whose value used a formatter extension command failed with
``Not supported field in StreamInfo``, even though the same command worked in ``body`` and
``attributes``. The configured command parsers are now passed through to custom-tag creation.
