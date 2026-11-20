The OpenTelemetry ``trace_id_ratio_based`` sampler now reads the 7 rightmost bytes of the trace ID
instead of the first 8, and samples a trace when that value is at or above a threshold derived from
``sampling_percentage``. Previously, trace IDs that start with a timestamp sampled at a rate far
from the configured percentage. The sampled fraction for random trace IDs is unchanged, but the set
of sampled trace IDs differs. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.otel_ratio_sampler_use_trace_id_rightmost_bytes`` to ``false``.
