Added an opt-in :ref:`enable_retry_after_header
<envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.enable_retry_after_header>` option
to the global rate limit filter and an equivalent :ref:`local rate limit option
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.enable_retry_after_header>`.
For enforced 429 responses, enabling the option allows the filter to add a ``Retry-After`` header.
For the global rate limit filter, the rate limit service response must contain at least one
``OVER_LIMIT`` descriptor status; the header value is the largest ``duration_until_reset`` among
those statuses. For the local rate limit filter, the header value is the time until the token bucket
that rejected the request has a token available. Both values are expressed in seconds and clamped to
at least 1. Both filters preserve an existing ``Retry-After`` header. The option is disabled by
default and has no effect on responses with any status code other than 429.
