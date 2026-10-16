Added :ref:`resource_conversion
<envoy_v3_api_field_extensions.stat_sinks.open_telemetry.v3.SinkConfig.ConversionAction.resource_conversion>`
to the OpenTelemetry stats sink. It reports a converted metric under an OTLP resource of its own, built from
static attributes and from stat tags moved off the data points, so that one proxy can report the metrics of
each entity it serves, such as each pod on a node, under that entity's resource.
