Added HTTP local rate limit completion accounting for embedded ``rate_limits`` with
:ref:`apply_on_stream_done <envoy_v3_api_field_config.route.v3.RateLimit.apply_on_stream_done>`,
in both the filter-level and per-route configuration. These actions update matching descriptor
token buckets when the stream completes and remain skipped on the request path. The default local
token bucket continues to apply only on the request path. Route and virtual-host rate limit entries
are unchanged and continue to apply on the request path.

Missing or empty formatted completion usage now skips the descriptor without warning in both
global and local rate limiting. Invalid nonempty usage still logs a warning.
