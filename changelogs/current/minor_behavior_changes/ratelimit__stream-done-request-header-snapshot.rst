The global rate limit filter now saves a copy of the request headers when it first handles
a request, and uses that copy for :ref:`apply_on_stream_done
<envoy_v3_api_field_config.route.v3.RateLimit.apply_on_stream_done>` rate limits.
Previously these rate limits read the request headers when the request finished, after the
router or other filters may have removed or changed them (for example with
``request_headers_to_remove`` or path rewrites), so the rate limit could be skipped or
counted under the wrong key. Headers added by the rate limit service are included in the
copy, but headers added or changed later by other filters are not. This behavior can be
temporarily reverted by setting runtime guard
``envoy.reloadable_features.ratelimit_stream_done_use_decode_time_request_headers`` to
``false``.
