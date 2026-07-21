Added an opt-in :ref:`enable_retry_after_header
<envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.enable_retry_after_header>` option
to the global rate limit filter and an equivalent :ref:`local rate limit option
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.enable_retry_after_header>`.
When a filter enforces a 429 response, it can now emit a ``Retry-After`` header containing the number
of seconds until the applicable rate limit resets.
