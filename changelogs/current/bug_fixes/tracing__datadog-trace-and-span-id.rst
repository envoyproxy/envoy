Fixed the Datadog tracer returning the span ID as the trace ID, for example in the ``%TRACE_ID%``
access log formatter. For a trace continued from an incoming request, the value was the ID of
Envoy's span rather than of the trace, and for a trace started by Envoy, it was only the low 64
bits of the trace ID. The trace ID is now the full 128-bit ID as 32 zero-padded hex characters, and
``%SPAN_ID%``, which was always empty with the Datadog tracer, is now the span ID as 16 zero-padded
hex characters. This is the same format as the W3C ``traceparent`` header.
