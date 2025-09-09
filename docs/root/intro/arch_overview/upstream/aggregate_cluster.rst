.. _arch_overview_aggregate_cluster:

Aggregate Cluster
=================

An aggregate cluster allows you to set up failover between multiple upstream clusters that have different
configurations. For example, you might switch from an :ref:`EDS <arch_overview_service_discovery_types_eds>` cluster to
a :ref:`STRICT_DNS <arch_overview_service_discovery_types_strict_dns>` cluster, or from a cluster using
:ref:`ROUND_ROBIN <arch_overview_load_balancing_types_round_robin>` load balancing to one using
:ref:`MAGLEV <arch_overview_load_balancing_types_maglev>`. You can also use it to change timeouts, such as moving from
a ``0.1s`` connection timeout to a ``1s`` timeout.

To enable this failover, the aggregate cluster references other clusters by their names in the
:ref:`configuration <envoy_v3_api_msg_extensions.clusters.aggregate.v3.ClusterConfig>`. The ordering of these clusters
in the :ref:`clusters list <envoy_v3_api_field_extensions.clusters.aggregate.v3.ClusterConfig.clusters>` implicitly
defines the fallback priority.

The aggregate cluster uses a tiered approach to load balancing:

* At the top level, it decides which cluster and priority to use.
* It then hands off the actual load balancing to the selected cluster's own load balancer.

Internally, this top-level load balancer treats all the priorities across all referenced clusters as a single linear
list. By doing so, it reuses the existing load balancing algorithm and makes it possible to seamlessly shift traffic
between clusters as needed.

Linearize Priority Set
----------------------

Upstream hosts are grouped into different :ref:`priority levels <arch_overview_load_balancing_priority_levels>`, and
each level includes hosts that can be healthy, degraded, or unhealthy. To simplify host selection during load balancing,
linearization merges these priority levels across multiple clusters into a single sequence.

For example, if the primary cluster has three priority levels, and the secondary and tertiary clusters each have two,
the failover order is:

* Primary
* Secondary
* Tertiary

+-----------+----------------+-------------------------------------+
| Cluster   | Priority Level |  Priority Level after Linearization |
+===========+================+=====================================+
| Primary   | 0              |  0                                  |
+-----------+----------------+-------------------------------------+
| Primary   | 1              |  1                                  |
+-----------+----------------+-------------------------------------+
| Primary   | 2              |  2                                  |
+-----------+----------------+-------------------------------------+
| Secondary | 0              |  3                                  |
+-----------+----------------+-------------------------------------+
| Secondary | 1              |  4                                  |
+-----------+----------------+-------------------------------------+
| Tertiary  | 0              |  5                                  |
+-----------+----------------+-------------------------------------+
| Tertiary  | 1              |  6                                  |
+-----------+----------------+-------------------------------------+

This approach ensures a straightforward way to decide which hosts receive traffic based on priority, even when working
with multiple clusters.

Example
-------

A sample aggregate cluster configuration could be:

.. code-block:: yaml

  name: aggregate_cluster
  connect_timeout: 0.25s
  lb_policy: CLUSTER_PROVIDED
  cluster_type:
    name: envoy.clusters.aggregate
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.clusters.aggregate.v3.ClusterConfig
      clusters:
      # cluster primary, secondary and tertiary should be defined outside.
      - primary
      - secondary
      - tertiary

Important Considerations for Aggregate Clusters
-----------------------------------------------

Some features might not work as expected with aggregate clusters. For example,

PriorityLoad Retry Plugins
^^^^^^^^^^^^^^^^^^^^^^^^^^

:ref:`PriorityLoad retry plugins <envoy_v3_api_field_config.route.v3.RetryPolicy.retry_priority>` will not work with an
aggregate cluster. Because the aggregate cluster's load balancer controls traffic distribution at a higher level, it
effectively overrides the PriorityLoad behavior during load balancing.

Stateful Sessions
^^^^^^^^^^^^^^^^^

:ref:`Stateful Sessions <envoy_v3_api_msg_extensions.filters.http.stateful_session.v3.StatefulSession>` rely on the
cluster to directly know the endpoint receiving traffic. With an aggregate cluster, the top-level load balancer selects
a cluster first, but does not track specific endpoints inside that cluster.

If we configure Stateful Sessions to override the upstream address, the load balancer bypasses its usual algorithm to
send traffic directly to that host. This works only when the cluster itself knows the exact endpoint.

In an aggregate cluster, the final routing decision happens one layer beneath the aggregate load balancer, so the filter
cannot locate that specific endpoint at the aggregate level. As a result, Stateful Sessions are incompatible with
aggregate clusters, because the final cluster choice is made without direct knowledge of the specific endpoint which
doesn't exist at the top level.

Circuit Breakers
^^^^^^^^^^^^^^^^

In general, an aggregate cluster should be thought of as a cluster that groups the endpoints of the underlying clusters
together for load balancing purposes only, not for circuit breaking which is handled at the level of the underlying clusters,
not at the level of the aggregate cluster itself. This allows the aggregate cluster to maintain its failover capabilities
whilst respecting the circuit breaker limits of each underlying cluster. This is intentional as the underlying clusters
are accessible through multiple paths (directly or via the aggregate cluster) and configuring aggregate cluster circuit
breakers would effectively double the circuit breaker limits rendering them useless.

When the configured limit is reached on the underlying cluster(s) only the underlying cluster(s)' circuit breaker opens.
When an underlying cluster's circuit breaker opens, requests routed through the aggregate cluster to that underlying cluster
will be rejected. The aggregate cluster's circuit breaker remains closed at all times, regardless of whether the circuit
breaker(s) limits on the underlying cluster(s) are reached or not.

As an exception, the only circuit breaker configured at the aggregate cluster level is :ref:`max_retries <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_retries>`
because when Envoy processes a retry request, it needs to determine whether the retry limit has been exceeded before
the aggregate cluster is able to choose the underlying cluster to use. When the configured limit is reached the aggregate
cluster's circuit breaker opens, and subsequent requests to the aggregate cluster path cannot retry, even though the
underlying cluster's retry budget is still available.

Load Balancing Example
----------------------

Aggregate cluster uses tiered load balancing algorithm and the top tier is distributing traffic to different clusters
according to the health score across all :ref:`priorities <arch_overview_load_balancing_priority_levels>` in each
cluster. The aggregate cluster in this section includes two clusters which is different from what the above
configuration describes.

The aggregate cluster uses a tiered load balancing algorithm with two main steps:

* **Top Tier:** Distribute traffic across different clusters based on each cluster's overall health (across all
  :ref:`priorities <arch_overview_load_balancing_priority_levels>`).
* **Second Tier:** Once a cluster is chosen, delegate traffic distribution within that cluster to its own load balancer
  (e.g., :ref:`ROUND_ROBIN <arch_overview_load_balancing_types_round_robin>`,
  :ref:`MAGLEV <arch_overview_load_balancing_types_maglev>`, etc.).

+-----------------------------------------------------------------------------------------------------------------------+--------------------+----------------------+
| Cluster                                                                                                               | Traffic to Primary | Traffic to Secondary |
+=======================================================================+===============================================+====================+======================+
| Primary                                                               | Secondary                                     |                                           |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+                                           +
| P=0 Healthy Endpoints | P=1 Healthy Endpoints | P=2 Healthy Endpoints | P=0 Healthy Endpoints | P=1 Healthy Endpoints |                                           |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+--------------------+----------------------+
| 100%                  | 100%                  | 100%                  | 100%                  | 100%                  | 100%               | 0%                   |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+--------------------+----------------------+
| 72%                   | 100%                  | 100%                  | 100%                  | 100%                  | 100%               | 0%                   |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+--------------------+----------------------+
| 71%                   | 1%                    | 0%                    | 100%                  | 100%                  | 100%               | 0%                   |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+--------------------+----------------------+
| 71%                   | 0%                    | 0%                    | 100%                  | 100%                  | 99%                | 1%                   |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+--------------------+----------------------+
| 50%                   | 0%                    | 0%                    | 50%                   | 0%                    | 70%                | 30%                  |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+--------------------+----------------------+
| 20%                   | 20%                   | 10%                   | 25%                   | 25%                   | 70%                | 30%                  |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+--------------------+----------------------+
| 20%                   | 0%                    | 0%                    | 20%                   | 0%                    | 50%                | 50%                  |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+--------------------+----------------------+
| 0%                    | 0%                    | 0%                    | 100%                  | 0%                    | 0%                 | 100%                 |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+--------------------+----------------------+
| 0%                    | 0%                    | 0%                    | 72%                   | 0%                    | 0%                 | 100%                 |
+-----------------------+-----------------------+-----------------------+-----------------------+-----------------------+--------------------+----------------------+

.. note::
   By default, the :ref:`overprovisioning factor <arch_overview_load_balancing_overprovisioning_factor>` is **1.4**.
   This factor boosts lower health percentages to account for partial availability. For instance, if a priority level is
   **80%** healthy, multiplying by **1.4** results in **112%**, which is capped at **100%**. In other words, any product
   above **100%** is treated as **100%**.

The aggregate cluster load balancer first calculates each priority's health score for every cluster, sums those up,
and then assigns traffic based on the overall total. If the total is at least **100**, the combined traffic is capped at
**100%**. If it's below **100**, Envoy scales (normalizes) it so that the final distribution sums to **100%**.

Scenario A: Total Health ≥ 100
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Suppose we have two clusters:

* Primary with three priority levels: ``20%, 20%, 10%`` healthy.
* Secondary with two priority levels: ``25%, 25%`` healthy.

1. Compute raw health scores using ``percent_healthy × overprovisioning_factor (1.4)``, each capped at **100**.

   * Primary:

     * P=0: 20% × 1.4 = 28
     * P=1: 20% × 1.4 = 28
     * P=2: 10% × 1.4 = 14
     * **Sum:** 28 + 28 + 14 = 70

   * Secondary:

     * P=0: 25% × 1.4 = 35
     * P=1: 25% × 1.4 = 35
     * **Sum:** 35 + 35 = 70

2. Assign traffic to the first cluster, then the next, etc., without exceeding **100%** total.

   * Primary takes its 70% first.
   * Secondary then takes min(100 - 70, 70) = 30.
   * Combined total is 70 + 30 = 100.

3. Distribute that traffic internally by priority.

   * Primary's **70%** is split across its priorities in proportion to **28** : **28** : **14**, i.e.:

     * P=0 → 28%
     * P=1 → 28%
     * P=2 → 14%

   * Secondary's **30%** goes first to P=0, which is 35, but capped at whatever remains from 100 after primary
     took 70 (i.e., 30). So:

     * P=0 → 30%
     * P=1 → 0%

Hence the final breakdown of traffic is:

* Primary: ``{28%, 28%, 14%}``
* Secondary: ``{30%, 0%}``

Scenario B: Total Health < 100
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Sometimes the health scores add up to less than **100**. In that case, Envoy 'normalizes' them so that each cluster and
priority still receives a portion out of 100%.

For instance, consider:

* Primary: ``20%, 0%, 0%``
* Secondary: ``20%, 0%``

1. Compute raw health scores (same formula: ``percent_healthy × 1.4``, capped at **100**):

   * Primary:

     * P=0: 20% × 1.4 = 28
     * P=1: 0 → 0
     * P=2: 0 → 0
     * **Sum:** 28 + 0 + 0 = 28

   * Secondary:

     * P=0: 20% × 1.4 = 28
     * P=1: 0 → 0
     * **Sum:** 28 + 0 = 28

2. Total raw health = 28 + 28 = **56** (below 100).

3. Normalize so that the final total is 100%.

   * Both clusters end up at ``28 / 56 = 50%``.

Thus each cluster, primary and secondary, receives 50% of the traffic. And since all of each cluster's share is in
the **Priority 0** (28 points) and the others are 0, the final distribution is:

* Primary: ``{50%, 0%, 0%}``
* Secondary: ``{50%, 0%}``

These scenarios show how Envoy's aggregate cluster load balancer decides which cluster (and priority level) gets traffic,
depending on the overall health of the endpoints. When the summed health across all clusters and priorities reaches or
exceeds **100**, Envoy caps the total at **100%** and allocates accordingly. If the total is below **100**, Envoy scales
up proportionally so that all traffic still adds up to **100%**.

Within each cluster, priority levels are also respected and allocated traffic based on their computed health scores.

Putting It All Together
^^^^^^^^^^^^^^^^^^^^^^^^

To sum this up in pseudo algorithms:

* Calculates each priority level's health score using ``(healthy% × overprovisioning factor)``, capped at **100%**.
* Sums and optionally normalizes total health across clusters.
* Computes each cluster's share of overall traffic i.e. its "cluster priority load".
* Distributes traffic among the priorities within each cluster according to their health scores.
* Performs final load balancing within each cluster.

::

  health(P_X) = min(100, 1.4 * 100 * healthy_P_X_backends / total_P_X_backends), where
                  total_P_X_backends is the number of backends for priority P_X after linearization

  normalized_total_health = min(100, Σ(health(P_0)...health(P_X)))

  cluster_priority_load(C_0) = min(100, Σ(health(P_0)...health(P_k)) * 100 / normalized_total_health),
                  where P_0...P_k belong to C_0

  cluster_priority_load(C_X) = min(100 - Σ(priority_load(C_0)..priority_load(C_X-1)),
                           Σ(health(P_x)...health(P_X)) * 100 / normalized_total_health),
                           where P_x...P_X belong to C_X

  map from priorities to clusters:
    P_0 ... P_k ... ...P_x ... P_X
    ^       ^          ^       ^
    cluster C_0        cluster C_X

In the second tier of load balancing, Envoy hands off traffic to the cluster selected in the first tier. That cluster
can then apply any of the load balancing algorithms described in
:ref:`load balancer type <arch_overview_load_balancing_types>`.
