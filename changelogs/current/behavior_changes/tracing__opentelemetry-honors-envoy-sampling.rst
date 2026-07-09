The OpenTelemetry tracer now honors Envoy request-entry tracing decisions, including
:ref:`overall_sampling <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.Tracing.overall_sampling>`,
even when a propagated ``traceparent`` header or configured OpenTelemetry sampler indicates that a
span should be sampled. Configurations that previously relied on the tracer exporting spans in
those cases may export fewer OTLP spans after upgrade.
