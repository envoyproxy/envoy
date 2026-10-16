Added inline :ref:`token_bucket <envoy_v3_api_field_extensions.access_loggers.filters.process_ratelimit.v3.ProcessRateLimitFilter.token_bucket>`
configuration to the :ref:`process rate limit filter
<envoy_v3_api_msg_extensions.access_loggers.filters.process_ratelimit.v3.ProcessRateLimitFilter>`
as an alternative to the existing ``dynamic_config``. This allows configuring access log rate
limiting directly in the listener configuration without a separate config source.
