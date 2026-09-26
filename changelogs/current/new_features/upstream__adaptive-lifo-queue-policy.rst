Added an :ref:`adaptive LIFO queue policy
<envoy_v3_api_msg_extensions.queue_policy.adaptive_lifo.v3.AdaptiveLifoQueuePolicyConfig>` for
pending requests. The policy uses FIFO ordering below a configurable queue-size threshold and LIFO
ordering while the queue is overloaded.
