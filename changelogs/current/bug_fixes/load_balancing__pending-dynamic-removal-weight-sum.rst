Fixed ``ThreadAwareLoadBalancerBase`` triggering a false ``ENVOY_BUG`` and skipping weight
normalization when hosts retained with ``PENDING_DYNAMIC_REMOVAL`` temporarily push a
priority's combined host weight sum above ``UINT32_MAX``.
