Added a static :ref:`rate_limit <envoy_v3_api_field_config.route.v3.RateLimit.Override.rate_limit>`
source to the rate limit :ref:`limit <envoy_v3_api_field_config.route.v3.RateLimit.limit>` override.
This allows a fixed ``requests_per_unit``/``unit`` limit override to be attached to a descriptor
directly from configuration, without requiring the value to be sourced from ``dynamic_metadata``.
