Added :ref:`shadow_over_limit <envoy_v3_api_field_service.ratelimit.v3.RateLimitResponse.DescriptorStatus.shadow_over_limit>`
to the rate limit service descriptor status. When any descriptor sets it, the HTTP, network and Thrift rate limit
filters increment the ``shadow_over_limit`` counter, so descriptors over limit in shadow mode can be observed.
