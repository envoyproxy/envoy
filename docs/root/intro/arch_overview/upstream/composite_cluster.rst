.. _arch_overview_composite_cluster:

Composite cluster
=================

The composite cluster type provides retry-aware cluster selection, allowing different retry attempts
to automatically target different upstream clusters. Unlike the standard
:ref:`aggregate cluster <arch_overview_aggregate_cluster>` which uses health-based selection, the
composite cluster uses the retry attempt count to deterministically select which sub-cluster to route to.

Use cases
---------

The composite cluster addresses several important scenarios:

* **Retry-based progression**: Different clusters for retry attempts (primary → secondary → tertiary).
* **AI Gateway failover**: Route initial requests to preferred providers and retries to fallbacks.
* **Cost optimization**: Try expensive, high-performance services first, fall back to cheaper alternatives.

Configuration
-------------

The composite cluster is configured using the
:ref:`ClusterConfig <envoy_v3_api_msg_extensions.clusters.composite.v3.ClusterConfig>`.

Example configuration
~~~~~~~~~~~~~~~~~~~~~

The following example shows a composite cluster with three sub-clusters:

.. code-block:: yaml

  name: composite_cluster
  connect_timeout: 0.25s
  lb_policy: CLUSTER_PROVIDED
  cluster_type:
    name: envoy.clusters.composite
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
      clusters:
      - name: primary_cluster
      - name: secondary_cluster
      - name: fallback_cluster

In this configuration:

- Initial requests (attempt 1) go to ``primary_cluster``.
- First retries (attempt 2) go to ``secondary_cluster``.
- Second retries (attempt 3) go to ``fallback_cluster``.
- Further retry attempts (attempt 4+) will fail with no host available.

Cluster selection
-----------------

The composite cluster uses a sequential selection strategy based on retry attempt count:

* **Initial request** (attempt 1): Uses the first cluster.
* **First retry** (attempt 2): Uses the second cluster.
* **Second retry** (attempt 3): Uses the third cluster.
* **Further retries**: Fail with no host available.

When retry attempts exceed the number of configured clusters, requests fail with no host available.
Configure the number of retries in your retry policy to match your cluster configuration.

Retry policy coordination
-------------------------

For the composite cluster to function correctly, configure an appropriate
:ref:`retry policy <envoy_v3_api_msg_config.route.v3.RetryPolicy>` at the route level:

.. code-block:: yaml

  retry_policy:
    retry_on: "5xx,gateway-error,connect-failure,refused-stream"
    num_retries: 2  # Enables attempts 1, 2, and 3 (3 total attempts)

Important considerations
------------------------

* **Sub-cluster independence**: Each sub-cluster maintains its own health checking, load balancing,
  and outlier detection. If a selected sub-cluster has no healthy hosts available, the request will
  fail according to that sub-cluster's load balancing behavior, potentially triggering another retry
  attempt if configured.
* **Deterministic routing**: The same retry attempt will always target the same cluster given
  identical configuration. Cluster selection is based solely on retry attempt count, not on the
  health status of sub-clusters.
* **Thread-local clustering**: Cluster selection occurs at the thread-local level for optimal
  performance.
* **Sub-cluster health**: Unlike the aggregate cluster, the composite cluster does not consider
  sub-cluster health when selecting which cluster to use. Each retry attempt targets a specific
  cluster based on attempt count, regardless of whether that cluster has healthy endpoints available.

Comparison with aggregate cluster
---------------------------------

+------------------+---------------------------+-------------------------------+
| Feature          | Aggregate Cluster         | Composite Cluster             |
+==================+===========================+===============================+
| Selection basis  | Health status             | Retry attempt count           |
+------------------+---------------------------+-------------------------------+
| Primary use case | Health-based failover     | Retry progression             |
+------------------+---------------------------+-------------------------------+
| Overflow handling| Health-dependent          | Fails request                 |
+------------------+---------------------------+-------------------------------+
| Predictability   | Health-dependent          | Fully deterministic           |
+------------------+---------------------------+-------------------------------+


