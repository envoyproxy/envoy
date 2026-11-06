Added :ref:`stop_on_match <envoy_v3_api_field_config.route.v3.RateLimit.stop_on_match>` to the route
rate limit configuration. When set on an entry that produces a descriptor, the HTTP global and local
rate limit filters skip the later entries, which allows ordered, tiered limits without negating
earlier entries.
