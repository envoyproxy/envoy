tracing: the OpenTelemetry tracer now populates the ``flags`` field on exported OTLP spans. The low 8 bits carry the W3C
trace flags of the span (currently only the sampled bit), and bits 8 and 9 record whether the span's parent context was
remote, as defined by the OTLP specification. Previously the field was always 0, which OTLP consumers interpret as
"trace flags not recorded".
