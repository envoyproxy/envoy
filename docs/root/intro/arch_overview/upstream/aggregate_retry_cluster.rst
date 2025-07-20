.. _arch_overview_aggregate_retry_cluster:

Aggregate Retry Cluster
=======================

An aggregate retry cluster enables retry-aware cluster selection, allowing different retry attempts to automatically
target different upstream clusters. This is particularly useful for scenarios where we want to implement intelligent
failover between different providers based on retry attempts. For example, we might start with a high-performance
provider and automatically fall back to a more reliable but slower provider on retries.

Unlike the standard :ref:`aggregate cluster <arch_overview_aggregate_cluster>` which uses health-based selection,
the aggregate retry cluster uses the retry attempt count to deterministically select which subcluster to route to.
This ensures consistent behavior where the first attempt always goes to the primary cluster, the second attempt
goes to the secondary cluster, and so on.

The aggregate retry cluster references other clusters by their names in the
:ref:`configuration <envoy_v3_api_msg_extensions.clusters.aggregate_retry.v3.ClusterConfig>`. The ordering of these
clusters in the :ref:`clusters list <envoy_v3_api_field_extensions.clusters.aggregate_retry.v3.ClusterConfig.clusters>`
defines the retry-based routing priority.

Retry-Aware Cluster Selection
-----------------------------

The aggregate retry cluster maps retry attempts to specific subclusters using a simple but effective algorithm:

* **First attempt (retry count = 0):** Routes to the first cluster in the list
* **Second attempt (retry count = 1):** Routes to the second cluster in the list
* **Subsequent attempts:** Continue through the list, with overflow behavior configurable

The cluster extracts the retry attempt count from the load balancer context's stream info, specifically from the
``host_selection_retry_count`` field. This ensures that retry routing decisions are made consistently across
different load balancing scenarios.

Retry Overflow Behavior
^^^^^^^^^^^^^^^^^^^^^^^

When the retry attempt count exceeds the number of configured clusters, the aggregate retry cluster supports
two overflow behaviors:

* **FIRST_CLUSTER (default):** Route all overflow attempts to the first cluster
* **LAST_CLUSTER:** Route all overflow attempts to the last cluster

This provides flexibility in handling scenarios where more retries are attempted than there are configured clusters.

Example
-------

A sample aggregate retry cluster configuration for AI Gateway failover:

.. code-block:: yaml

  name: ai_gateway_cluster
  connect_timeout: 0.25s
  lb_policy: CLUSTER_PROVIDED
  cluster_type:
    name: envoy.clusters.aggregate_retry
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.clusters.aggregate_retry.v3.ClusterConfig
      clusters:
      # Clusters should be defined outside this configuration
      - primary        # High-performance, attempt 0
      - secondary      # Reliable fallback, attempt 1
      - local          # Local backup, attempt 2+
      retry_overflow_behavior: LAST_CLUSTER

In this configuration:
- First requests (retry count 0) go to ``primary``.
- First retries (retry count 1) go to ``secondary``.
- Subsequent retries (retry count 2+) go to ``local`` due to ``LAST_CLUSTER`` overflow behavior.

Connection Lifetime Callbacks
-----------------------------

The aggregate retry cluster automatically aggregates connection lifetime callbacks from all configured subclusters.
This ensures that connection monitoring and management features work seamlessly across all underlying clusters.

When subclusters are added or removed, the aggregate retry cluster automatically updates its callback aggregation
to maintain comprehensive connection lifecycle visibility.

Important Considerations
------------------------

Retry Policy Configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^

For the aggregate retry cluster to function correctly, you must configure an appropriate
:ref:`retry policy <envoy_v3_api_msg_config.route.v3.RetryPolicy>` at the route level. The number of retries
should correspond to the number of clusters you want to utilize.

.. code-block:: yaml

  retry_policy:
    retry_on: "5xx,gateway-error,connect-failure,refused-stream"
    num_retries: 2  # Enables attempts 0, 1, and 2 (3 total attempts)

Deterministic Routing
^^^^^^^^^^^^^^^^^^^^^

Unlike health-based load balancing, the aggregate retry cluster provides deterministic routing based on retry attempt
count. This means:

* The same retry attempt will always target the same cluster (given the same configuration).
* Circuit breaker states and cluster health do not affect cluster selection.
* Routing decisions are predictable and debuggable.

Load Balancing Integration
^^^^^^^^^^^^^^^^^^^^^^^^^^

The aggregate retry cluster integrates with Envoy's load balancing infrastructure by:

* Implementing a custom load balancer context that tracks retry attempt information.
* Delegating actual host selection to the selected subcluster's load balancer.
* Maintaining compatibility with all existing load balancing policies.

Circuit Breakers and Health Checks
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Each subcluster maintains its own circuit breaker state and health check status. The aggregate retry cluster
respects these states when delegating to subclusters:

* If a selected subcluster's circuit breaker is open, the request will be rejected.
* Health check failures in subclusters are handled by their respective load balancers.
* The aggregate cluster itself does not implement cluster-level circuit breaking.

Comparison with Standard Aggregate Cluster
------------------------------------------

+---------------------------+-------------------------+--------------------------------+
| Feature                   | Aggregate Cluster       | Aggregate Retry Cluster        |
+===========================+=========================+================================+
| Selection Criteria        | Cluster health scores   | Retry attempt count            |
+---------------------------+-------------------------+--------------------------------+
| Traffic Distribution      | Health-proportional     | Deterministic per retry        |
+---------------------------+-------------------------+--------------------------------+
| Primary Use Case          | Load balancing failover | AI Gateway retry failover      |
+---------------------------+-------------------------+--------------------------------+
| Predictability            | Health-dependent        | Fully deterministic            |
+---------------------------+-------------------------+--------------------------------+
| Priority Linearization    | Yes                     | No (delegates to subclusters)  |
+---------------------------+-------------------------+--------------------------------+

Use Cases
---------

AI Gateway Scenarios
^^^^^^^^^^^^^^^^^^^^

The aggregate retry cluster is designed for AI Gateway deployments where different AI providers have different
characteristics:

* **Primary Provider:** High-performance, potentially less reliable.
* **Secondary Provider:** More reliable, potentially higher latency.
* **Tertiary Provider:** Local or backup model with guaranteed availability.

Cost Optimization
^^^^^^^^^^^^^^^^^^

By routing initial attempts to preferred (often more expensive) providers and retries to alternative providers,
the aggregate retry cluster enables cost optimization while maintaining service reliability.
