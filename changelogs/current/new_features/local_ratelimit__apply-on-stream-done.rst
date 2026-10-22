Added HTTP local rate limit completion accounting for embedded ``rate_limits`` with
:ref:`apply_on_stream_done <envoy_v3_api_field_config.route.v3.RateLimit.apply_on_stream_done>`.
These actions update matching descriptor token buckets when the stream completes and remain
skipped on the request path. No runtime override is required. The default local token bucket
continues to apply only on the request path.

Set the disabled-by-default runtime guard
``envoy.reloadable_features.local_rate_limit_route_policy_apply_on_stream_done`` to ``true`` to honor
:ref:`apply_on_stream_done <envoy_v3_api_field_config.route.v3.RateLimit.apply_on_stream_done>`
for route and virtual-host rate limit entries.

Missing or empty formatted completion usage now skips the descriptor without warning in both
global and local rate limiting. Invalid nonempty usage still logs a warning.
