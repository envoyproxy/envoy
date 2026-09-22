.. _config_cluster_manager_pending_request_queueing:

Pending request queueing
========================

When no upstream connection is immediately available, a connection pool may queue a request while
it establishes a connection or waits for connection capacity. The
:ref:`maximum pending requests circuit breaker
<envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_pending_requests>` limits how
many requests may wait. A queue policy controls the order in which those requests are selected when
capacity becomes available; it does not change the circuit breaker limit.

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
ordering while the queue size is below the configured threshold. When the queue size reaches the
threshold, it selects the most recently queued request until the queue size falls below the
threshold again.

Adaptive LIFO prioritizes newer requests during overload. Older requests may wait longer, and under
sustained overload they may time out before being selected.

The following cluster fragment configures adaptive LIFO with a threshold of 100 pending requests:

.. code-block:: yaml

  queuing_policies:
    pending_rq_policy:
      name: envoy.queue_policy.adaptive_lifo
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.queue_policy.adaptive_lifo.v3.AdaptiveLifoQueuePolicyConfig
        lifo_switch_threshold: 100

Observability
-------------

The ``upstream_rq_pending_active`` gauge reports the number of requests currently waiting for a
connection pool connection. For queue policies that report an overloaded state, such as adaptive
LIFO, ``upstream_queue_overloaded`` reports the number of connection pool pending request queues
currently overloaded. Both are documented in the :ref:`cluster statistics
<config_cluster_manager_cluster_stats>`.
