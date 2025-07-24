.. _arch_overview_composite_cluster:

Composite cluster
=================

The composite cluster type provides flexible sub-cluster selection strategies for various use cases. Unlike the standard :ref:`aggregate cluster <arch_overview_load_balancing_types_aggregate>` which performs health-based failover, the composite cluster implements configurable cluster selection strategies.

.. note::

  The composite cluster is not related to load balancing in the traditional sense as it cannot be configured with specific health checking or outlier detection settings like other clusters. Instead, health checking and outlier detection are performed by the sub-clusters, and the composite cluster provides the ability to select among them based on configurable strategies.

Use cases
---------

The composite cluster addresses several important scenarios:

* **Retry-based progression**: Different clusters for retry attempts (primary → secondary → tertiary)
* **Cross-cluster failover**: More flexible alternative to traditional aggregate clusters
* **Future extensibility**: Potential for stateful session affinity and advanced routing strategies
* **Cluster specifier replacement**: More flexible routing decisions than weighted clusters

Configuration
-------------

The composite cluster is configured using the :ref:`composite cluster configuration <envoy_v3_api_msg_extensions.clusters.composite.v3.ClusterConfig>`. This includes a list of clusters with their individual configurations, a selection strategy, and overflow behavior settings.

Example configuration
~~~~~~~~~~~~~~~~~~~~~

The following example shows a composite cluster with three sub-clusters configured for retry progression:

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
      - name: tertiary_cluster
      selection_strategy: SEQUENTIAL
      retry_overflow_option: ROUND_ROBIN

Selection strategies
--------------------

Currently, the composite cluster supports the SEQUENTIAL selection strategy:

SEQUENTIAL
~~~~~~~~~~

The SEQUENTIAL strategy selects clusters in order based on the retry attempt number:

* **Initial request** (attempt 1): Uses the first cluster
* **First retry** (attempt 2): Uses the second cluster
* **Second retry** (attempt 3): Uses the third cluster
* **Further retries**: Handled according to the overflow option

This strategy is ideal for retry-based progression where you want to try different clusters in a specific order.

.. note::

  Future versions may support additional strategies like WEIGHTED_RANDOM for probabilistic selection, LEAST_REQUEST for load-based selection, CONSISTENT_HASH for session affinity, and HEALTH_BASED for health-aware routing.

Overflow handling
-----------------

When retry attempts exceed the number of configured clusters, the behavior is controlled by the ``retry_overflow_option``:

FAIL
~~~~

Further retry attempts fail with no host available. This is the default behavior and ensures strict adherence to the configured cluster progression.

USE_LAST_CLUSTER
~~~~~~~~~~~~~~~~~

Continue using the last cluster in the list for all subsequent retry attempts. This provides a fallback mechanism while maintaining the progression for earlier retries.

ROUND_ROBIN
~~~~~~~~~~~

Round-robin through all available clusters for overflow attempts. For example, with 3 clusters, the 4th attempt uses cluster 1, the 5th uses cluster 2, etc. This provides continued load distribution even for excessive retry scenarios.

Connection lifetime callbacks
-----------------------------

The composite cluster aggregates connection lifetime callbacks from all sub-clusters, providing a unified interface for monitoring connection events across the entire cluster set. This ensures that upstream connection monitoring works seamlessly regardless of which sub-cluster is selected.

Important considerations
------------------------

* **Sub-cluster independence**: Each sub-cluster maintains its own health checking, load balancing, and outlier detection
* **Retry policy coordination**: The composite cluster works with Envoy's retry policies to determine retry attempt numbers
* **Thread-local clustering**: The cluster selection occurs at the thread-local level for optimal performance
* **Configuration validation**: All sub-clusters must exist and be properly configured

Comparison with aggregate cluster
---------------------------------

+------------------+---------------------------+-------------------------------+
| Feature          | Aggregate Cluster         | Composite Cluster             |
+==================+===========================+===============================+
| Selection basis  | Health status             | Retry attempts / Strategy     |
+------------------+---------------------------+-------------------------------+
| Primary use case | Health-based failover     | Retry progression & routing   |
+------------------+---------------------------+-------------------------------+
| Extensibility    | Limited to health logic   | Configurable strategies       |
+------------------+---------------------------+-------------------------------+
| Overflow handling| Health-dependent          | Configurable (FAIL/LAST/RR)   |
+------------------+---------------------------+-------------------------------+
| Future potential | Health improvements       | Session affinity, weighting   |
+------------------+---------------------------+-------------------------------+
