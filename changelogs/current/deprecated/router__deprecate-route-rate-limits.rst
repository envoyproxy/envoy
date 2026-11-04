Deprecated the route level :ref:`rate_limits <envoy_v3_api_field_config.route.v3.RouteAction.rate_limits>`
and virtual host level :ref:`rate_limits <envoy_v3_api_field_config.route.v3.VirtualHost.rate_limits>`
fields. Use :ref:`RateLimitPerRoute.rate_limits
<envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimitPerRoute.rate_limits>` in
``typed_per_filter_config`` or the filter level :ref:`rate_limits
<envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.rate_limits>` of the rate limit
filter instead, or :ref:`rate_limits
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.rate_limits>` of the
local rate limit filter.
