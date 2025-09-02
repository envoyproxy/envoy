.. _arch_overview_composite_cluster:

Composite Cluster
=================

A composite cluster enables retry progression across multiple upstream clusters. Unlike the :ref:`aggregate cluster <arch_overview_aggregate_cluster>`, which combines host sets for load balancing, the composite cluster selects exactly one sub-cluster per request attempt based on the retry count.

The composite cluster is useful when different retry attempts should target completely different upstream services or configurations. For example, you might route the initial request to a high-performance cache cluster, the first retry to a standard database cluster, and the second retry to a fallback read-replica cluster.

Configuration
-------------

The composite cluster references other clusters by their names in the :ref:`configuration <envoy_v3_api_msg_extensions.clusters.composite.v3.ClusterConfig>`. The ordering of these clusters in the :ref:`clusters list <envoy_v3_api_field_extensions.clusters.composite.v3.ClusterConfig.clusters>` determines the retry progression:

* **Initial request** (attempt #1): Uses the first cluster in the list
* **First retry** (attempt #2): Uses the second cluster in the list  
* **Second retry** (attempt #3): Uses the third cluster in the list
* **Further retries**: Behavior determined by :ref:`overflow_option <envoy_v3_api_field_extensions.clusters.composite.v3.ClusterConfig.overflow_option>`

The composite cluster works in conjunction with Envoy's :ref:`retry policies <envoy_v3_api_field_config.route.v3.RetryPolicy.retry_on>`. The route configuration determines *when* to retry (e.g., 5xx responses, connection failures), while the composite cluster determines *where* to send each retry attempt.

Retry Progression
-----------------

Each request attempt targets exactly one sub-cluster. The composite cluster does not combine host sets across clusters or load balance across multiple clusters simultaneously. This provides clear isolation between retry attempts and allows each sub-cluster to maintain its own:

* Health checking and outlier detection
* Circuit breakers and connection pools
* Load balancing algorithms
* TLS settings and authentication
* Timeouts and retry policies

If a sub-cluster has no healthy endpoints, the load balancer selection will return no host, which typically results in an immediate failure unless the route retry policy retries for such conditions (e.g., ``connect-failure`` or ``reset``).

Overflow Behavior
-----------------

When retry attempts exceed the number of configured sub-clusters, the behavior is determined by the :ref:`overflow_option <envoy_v3_api_field_extensions.clusters.composite.v3.ClusterConfig.overflow_option>`:

* ``FAIL`` (default): Further retry attempts fail with no host available
* ``USE_LAST_CLUSTER``: Continue using the last cluster in the list for overflow attempts
* ``ROUND_ROBIN``: Cycle through all clusters for overflow attempts

Example Configuration
---------------------

.. code-block:: yaml

  clusters:
  # Sub-clusters referenced by the composite cluster
  - name: cache_cluster
    connect_timeout: 0.25s
    type: STATIC
    load_assignment:
      cluster_name: cache_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address: { address: 127.0.0.1, port_value: 8080 }

  - name: database_cluster
    connect_timeout: 0.25s
    type: STATIC
    load_assignment:
      cluster_name: database_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address: { address: 127.0.0.1, port_value: 5432 }

  - name: fallback_cluster
    connect_timeout: 0.25s
    type: STATIC
    load_assignment:
      cluster_name: fallback_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address: { address: 127.0.0.1, port_value: 3306 }

  # The composite cluster
  - name: composite_cluster
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.composite
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
        clusters:
        - cache_cluster
        - database_cluster
        - fallback_cluster
        overflow_option: USE_LAST_CLUSTER

  # Route configuration with retry policy
  route_config:
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match:
          prefix: "/"
        route:
          cluster: composite_cluster
          retry_policy:
            retry_on: 5xx,connect-failure,refused-stream
            num_retries: 5

In this configuration:

1. Initial request goes to ``cache_cluster``
2. If that fails and triggers a retry, the first retry goes to ``database_cluster``  
3. If that fails, the second retry goes to ``fallback_cluster``
4. Any additional retries (up to the configured limit) continue using ``fallback_cluster`` due to the ``USE_LAST_CLUSTER`` overflow option

Comparison with Aggregate Cluster
----------------------------------

+---------------------------------+--------------------------------+----------------------------------+
| Feature                         | Composite Cluster              | Aggregate Cluster                |
+=================================+================================+==================================+
| **Load Balancing Scope**        | One cluster per request        | All clusters combined            |
+---------------------------------+--------------------------------+----------------------------------+
| **Retry Behavior**              | Sequential cluster progression | Priority-based failover          |
+---------------------------------+--------------------------------+----------------------------------+
| **Health Checking**             | Per-cluster isolation          | Combined health assessment       |
+---------------------------------+--------------------------------+----------------------------------+
| **Configuration Complexity**    | Simple cluster list            | Priority linearization           |
+---------------------------------+--------------------------------+----------------------------------+
| **Use Case**                    | Retry diversification          | Seamless failover                |
+---------------------------------+--------------------------------+----------------------------------+

Important Considerations
------------------------

Circuit Breakers
^^^^^^^^^^^^^^^^

Similar to the aggregate cluster, the composite cluster should be thought of as a cluster that selects among underlying clusters for load balancing purposes only. Circuit breaking is handled at the level of the underlying clusters, not at the level of the composite cluster itself. This allows the composite cluster to maintain its retry progression capabilities whilst respecting the circuit breaker limits of each underlying cluster.

When the configured limit is reached on an underlying cluster, only that cluster's circuit breaker opens. When an underlying cluster's circuit breaker opens, requests routed through the composite cluster to that underlying cluster will be rejected, potentially triggering a retry to the next cluster in the sequence.

The composite cluster's circuit breaker remains closed at all times, regardless of whether the circuit breaker limits on the underlying clusters are reached or not.

As with the aggregate cluster, the only circuit breaker configured at the composite cluster level is :ref:`max_retries <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_retries>` because when Envoy processes a retry request, it needs to determine whether the retry limit has been exceeded before the composite cluster is able to choose the underlying cluster to use.

Health Checking
^^^^^^^^^^^^^^^

Each sub-cluster maintains its own health checking independently. If a sub-cluster has no healthy endpoints when selected by the composite cluster, the request will typically fail unless the route retry policy is configured to retry on such conditions (e.g., ``no_healthy_upstream``).

The composite cluster does not perform health checking aggregation across sub-clusters, which provides clear isolation between retry attempts and allows each sub-cluster to maintain its own health state.

Stateful Sessions
^^^^^^^^^^^^^^^^^

:ref:`Stateful Sessions <envoy_v3_api_msg_extensions.filters.http.stateful_session.v3.StatefulSession>` are not compatible with composite clusters. Similar to aggregate clusters, the composite cluster's load balancer selects a sub-cluster first, but the stateful session filter cannot locate the specific endpoint at the composite level since the final routing decision happens within the selected sub-cluster.
