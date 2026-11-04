Added an :ref:`adaptive LIFO queue policy
<envoy_v3_api_msg_extensions.queue_policy.adaptive_lifo.v3.AdaptiveLifoQueuePolicyConfig>` for
pending requests. The policy uses FIFO ordering until a connection pool's queue reaches a
configured high watermark and LIFO ordering until the size reaches a lower configured watermark.
Separate watermarks avoid repeated order changes near the overload boundary.
