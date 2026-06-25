Added :ref:`configured_sample_rate <envoy_v3_api_field_data.tap.v3.TraceWrapper.configured_sample_rate>`
to the trace wrapper so consumers can recover the sampling rate that was configured when a trace
was emitted. Set on the first segment of each tap stream when ``tap_enabled`` is configured;
absent otherwise.
