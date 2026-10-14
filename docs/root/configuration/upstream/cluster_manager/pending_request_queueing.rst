.. _config_cluster_manager_pending_request_queueing:

Pending request queueing
========================

When no upstream connection is immediately available, a connection pool may queue a request while
it establishes a connection or waits for connection capacity. The
:ref:`maximum pending requests circuit breaker
<envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_pending_requests>` limits how
many requests may wait. A queue policy controls how those requests are selected when capacity
becomes available. A policy may also drop requests to shed load, but it does not change the circuit
breaker limit.

The policy is configured with
:ref:`Cluster.queuing_policies
<envoy_v3_api_field_config.cluster.v3.Cluster.queuing_policies>`. If no policy is configured, Envoy
uses FIFO ordering.

Queue policies
--------------

FIFO
~~~~

The :ref:`FIFO queue policy
<envoy_v3_api_msg_extensions.queue_policy.fifo.v3.FifoQueuePolicyConfig>` selects the oldest
pending request first.

Adaptive LIFO
~~~~~~~~~~~~~

The :ref:`adaptive LIFO queue policy
<envoy_v3_api_msg_extensions.queue_policy.adaptive_lifo.v3.AdaptiveLifoQueuePolicyConfig>` uses FIFO
ordering until the queue size reaches the configured high watermark. It then selects the most
recently queued request until the queue size reaches or falls below the low watermark. The
watermarks are absolute request counts applied independently to each connection pool's pending
request queue.
Separate watermarks prevent the policy from repeatedly switching order near the overload boundary.

Adaptive LIFO prioritizes newer requests during overload. Older requests may wait longer, and under
sustained overload they may time out before being selected.

The following cluster fragment configures each connection pool to enter LIFO mode when it has 80
pending requests and return to FIFO mode when it has 60 or fewer pending requests:

.. code-block:: yaml

  queuing_policies:
    pending_rq_policy:
      name: envoy.queue_policy.adaptive_lifo
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.queue_policy.adaptive_lifo.v3.AdaptiveLifoQueuePolicyConfig
        high_watermark: 80
        low_watermark: 60

CoDel
~~~~

The :ref:`CoDel queue policy
<envoy_v3_api_msg_extensions.queue_policy.codel.v3.CodelQueuePolicyConfig>` measures how long
requests wait in the pending queue. During each observation interval, it records the minimum queue
delay. If that minimum remains above the target delay for a complete interval, the queue is
considered persistently overloaded. CoDel then drops selected requests whose delay is more than
twice the target until queue delay recovers.

CoDel wraps another queue policy. The underlying policy selects the next request, while CoDel
decides whether to dispatch or drop it. FIFO is used when no underlying policy is configured. The
default target delay is 5 milliseconds and the default interval is 100 milliseconds.

The following cluster fragment uses adaptive LIFO ordering inside CoDel:

.. code-block:: yaml

  queuing_policies:
    pending_rq_policy:
      name: envoy.queue_policy.codel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.queue_policy.codel.v3.CodelQueuePolicyConfig
        target_delay: 0.005s
        interval: 0.100s
        underlying_queue_policy:
          name: envoy.queue_policy.adaptive_lifo
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.queue_policy.adaptive_lifo.v3.AdaptiveLifoQueuePolicyConfig
            high_watermark:
              value: 80
            low_watermark:
              value: 60

Observability
-------------

The ``upstream_rq_pending_active`` gauge reports the number of requests currently waiting for a
connection pool connection. For queue policies that report an overloaded state, such as adaptive
LIFO, ``upstream_queue_overloaded`` reports the number of connection pool pending request queues
currently overloaded. ``upstream_rq_pending_dropped_codel`` counts requests dropped by CoDel.
These statistics are documented in the :ref:`cluster statistics
<config_cluster_manager_cluster_stats>`.
