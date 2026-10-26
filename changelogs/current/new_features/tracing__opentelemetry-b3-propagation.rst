Added :ref:`incoming_trace_context <envoy_v3_api_field_config.trace.v3.OpenTelemetryConfig.incoming_trace_context>`
and :ref:`outgoing_trace_context <envoy_v3_api_field_config.trace.v3.OpenTelemetryConfig.outgoing_trace_context>`
to the OpenTelemetry tracer to configure which trace context formats are extracted from downstream requests
and injected into upstream requests. In addition to W3C Trace Context, the B3 single header and multiple
header formats are supported. The default remains W3C Trace Context only.
