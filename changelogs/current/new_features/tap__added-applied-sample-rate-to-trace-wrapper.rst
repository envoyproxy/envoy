Added :ref:`applied_sample_rate <envoy_v3_api_field_data.tap.v3.TraceWrapper.applied_sample_rate>`
to the trace wrapper so consumers can recover the sampling rate at which a trace was emitted. Set
on the first segment of each HTTP tap stream when ``tap_enabled`` is configured; absent otherwise.
