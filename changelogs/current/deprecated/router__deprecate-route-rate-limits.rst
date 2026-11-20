Deprecated the route level :ref:`rate_limits <envoy_v3_api_field_config.route.v3.RouteAction.rate_limits>`
and virtual host level :ref:`rate_limits <envoy_v3_api_field_config.route.v3.VirtualHost.rate_limits>`
fields. Use :ref:`RateLimitPerRoute.rate_limits
<envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimitPerRoute.rate_limits>` in
``typed_per_filter_config`` or the filter level :ref:`rate_limits
<envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.rate_limits>` of the rate limit
filter instead, or :ref:`rate_limits
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.rate_limits>` of the
local rate limit filter.
The fields that only apply to these deprecated fields are deprecated as well: the
:ref:`stage <envoy_v3_api_field_config.route.v3.RateLimit.stage>` and :ref:`disable_key
<envoy_v3_api_field_config.route.v3.RateLimit.disable_key>` of the rate limit config, ``stage`` of the
:ref:`rate limit filter <envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimit.stage>`
and the :ref:`local rate limit filter
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.stage>`,
:ref:`RateLimitPerRoute.vh_rate_limits
<envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimitPerRoute.vh_rate_limits>`
and :ref:`LocalRateLimit.vh_rate_limits
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.vh_rate_limits>`.
The never implemented ``RateLimitPerRoute.override_option`` field of the rate limit filter is
deprecated in favor of :ref:`rate_limits_override_option
<envoy_v3_api_field_extensions.filters.http.ratelimit.v3.RateLimitPerRoute.rate_limits_override_option>`.
