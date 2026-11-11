Added ``rate_limits_override_option`` to the rate limit filter (:ref:`filter level
<envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.rate_limits_override_option>` and :ref:`per-route
<envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimitPerRoute.rate_limits_override_option>`) and the
local rate limit filter (:ref:`rate_limits_override_option
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.rate_limits_override_option>`). When
set to ``ALL_LEVELS``, the embedded ``rate_limits`` of the configs at all levels are applied instead of only the most
specific one. The rate limit filter merges the generated descriptors into a single request, and the local rate limit
filter checks every level against its own descriptors and token buckets. For the local rate limit filter, setting
the option also ignores ``VirtualHost.rate_limits`` and ``RouteAction.rate_limits``, and it cannot be combined with
``local_rate_limit_per_downstream_connection``.
