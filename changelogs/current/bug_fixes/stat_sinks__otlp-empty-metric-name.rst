Fixed the OpenTelemetry stats sink reporting the metrics of a :ref:`ConversionAction
<envoy_v3_api_msg_extensions.stat_sinks.open_telemetry.v3.SinkConfig.ConversionAction>` without a ``metric_name``
with an empty name, which is not a valid OTLP metric name. Such metrics are now named as if their stats matched no
conversion.
