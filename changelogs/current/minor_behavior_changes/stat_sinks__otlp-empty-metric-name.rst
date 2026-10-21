A :ref:`ConversionAction <envoy_v3_api_msg_extensions.stat_sinks.open_telemetry.v3.SinkConfig.ConversionAction>` of the
OpenTelemetry stats sink with an empty ``metric_name`` now keeps the name of the stat, instead of reporting a metric
with an empty name.
