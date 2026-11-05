The OpenTelemetry ``trace_id_ratio_based`` sampler now takes its randomness from the 7 rightmost
bytes of the trace ID and samples a trace when that value is at or above a rejection threshold
derived from ``sampling_percentage``. This follows W3C Trace Context Level 2 and the OpenTelemetry
consistent probability sampling specification. Previously it read the first 8 bytes, so a trace ID
whose high bits are not random, such as one that starts with a timestamp, sampled at a rate far
from the configured percentage. The sampled fraction for random trace IDs is unchanged, but the
set of sampled trace IDs differs. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.otel_ratio_sampler_use_trace_id_rightmost_bytes`` to ``false``.
