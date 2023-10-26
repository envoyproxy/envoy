.. _config_stat_sinks_open_telemetry:

OpenTelemetry Stat Sink
=========================

The :ref:`OpenTelemetryStatsSink <envoy_v3_api_msg_extensions.stat_sinks.open_telemetry.v3.SinkConfig>` configuration specifies a
stat sink that emits stats according to `OpenTelemetry Protocol Specification <https://opentelemetry.io/docs/reference/specification/protocol/otlp/>`_.
The export requests of this sink are sent to the collector service according to the Protobuf defined in
`MetricService/Export <https://github.com/open-telemetry/opentelemetry-proto/blob/main/opentelemetry/proto/collector/metrics/v1/metrics_service.proto>`_.
The metric resources exported are defined in `metrics.proto <https://github.com/open-telemetry/opentelemetry-proto/blob/main/opentelemetry/proto/metrics/v1/metrics.proto>`_.
Any export request that this sink sends, will contain a single ``ResourceMetrics`` message, a single ``ScopeMetrics`` and repeated ``MetricRecord``,
according to the number of metrics collected during the proccess run.
