Added the :ref:`fair queue policy
<envoy_v3_api_msg_extensions.queue_policy.fair_queue.v3.FairQueuePolicyConfig>`
(``envoy.queue_policy.fair_queue``) pending request queue policy extension. The extension uses
Stochastic Fair Queuing (SFQ) with power-of-two-choices bucket selection over ``StreamInfo`` dynamic
metadata to isolate tenant classes when upstream connection pools queue pending requests.
