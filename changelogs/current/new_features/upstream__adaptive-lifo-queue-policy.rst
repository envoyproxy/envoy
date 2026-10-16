Added an :ref:`adaptive LIFO queue policy
<envoy_v3_api_msg_extensions.queue_policy.adaptive_lifo.v3.AdaptiveLifoQueuePolicyConfig>` for
pending requests. The policy uses FIFO ordering until the queue size reaches a configured
percentage of the pending request circuit breaker limit and LIFO ordering until the size reaches a
lower configured percentage. Separate watermarks avoid repeated order changes near the overload
boundary.
