Added a :ref:`CoDel queue policy
<envoy_v3_api_msg_extensions.queue_policy.codel.v3.CodelQueuePolicyConfig>` for pending requests.
The policy detects persistently high queue delay and drops delayed requests to shed load. It wraps
another queue policy, allowing CoDel admission control to be combined with FIFO or adaptive LIFO
ordering.
