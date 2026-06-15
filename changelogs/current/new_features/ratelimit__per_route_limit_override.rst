The HTTP :ref:`rate limit filter <config_http_filters_rate_limit>` now supports the
:ref:`limit <envoy_v3_api_field_config.route.v3.RateLimit.limit>` override (including the
``dynamic_metadata`` source) when the rate limit configuration is supplied via the filter's
``rate_limits`` field or the per-route
:ref:`RateLimitPerRoute <envoy_v3_api_msg_extensions.filters.http.ratelimit.v3.RateLimitPerRoute>`.
This allows the per-descriptor limit override and the per-request
:ref:`hits_addend <envoy_v3_api_field_config.route.v3.RateLimit.hits_addend>` to be used
together on the same rule.
