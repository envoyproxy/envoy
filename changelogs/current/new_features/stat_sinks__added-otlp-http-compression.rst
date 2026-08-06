Added :ref:`compression
<envoy_v3_api_field_extensions.stat_sinks.open_telemetry.v3.SinkConfig.compression>`
configuration to the OpenTelemetry stat sink. When set to ``GZIP``, the serialized OTLP payload is
gzip-compressed and ``Content-Encoding: gzip`` is set on the outbound OTLP/HTTP request to reduce
network bandwidth. Defaults to ``NONE`` for backward compatibility, and is only honored on the
``http_service`` export path (the OTLP/HTTP collector must support gzip decoding).
