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
* It then hands off the actual load balancing to the selected cluster’s own load balancer.

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
aggregate cluster. Because the aggregate cluster’s load balancer controls traffic distribution at a higher level, it
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
doesn’t exist at the top level.

Load Balancing Example
----------------------

Aggregate cluster uses tiered load balancing algorithm and the top tier is distributing traffic to different clusters
according to the health score across all :ref:`priorities <arch_overview_load_balancing_priority_levels>` in each
cluster. The aggregate cluster in this section includes two clusters which is different from what the above
configuration describes.

The aggregate cluster uses a tiered load balancing algorithm with two main steps:

* **Top Tier:** Distribute traffic across different clusters based on each cluster’s overall health (across all
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
   By default, the :ref:`overprovisioning factor <arch_overview_load_balancing_overprovisioning_factor>` is ``1.4``.
   This factor boosts lower health percentages to account for partial availability. For instance, if a priority level is
   ``80%`` healthy, multiplying by ``1.4`` results in ``112%``, which is capped at ``100%``. In other words, any product
   above ``100%`` is treated as ``100%``.

The example shows how the aggregate cluster level load balancer selects the cluster. E.g., healths
of {{20, 20, 10}, {25, 25}} would result in a priority load of {{28%, 28%, 14%}, {30%, 0%}} of
traffic. When normalized total health drops below 100, traffic is distributed after normalizing the
levels' health scores to that sub-100 total. E.g. healths of {{20, 0, 0}, {20, 0}} (yielding a
normalized total health of 56) would be normalized and each cluster will receive 20 * 1.4 / 56 = 50%
of the traffic which results in a priority load of {{50%, 0%, 0%}, {50%, 0%, 0%}} of traffic.

The load balancer reuses priority level logic to help with the cluster selection. The priority level
logic works with integer health scores. The health score of a level is (percent of healthy hosts in
the level) * (overprovisioning factor), capped at 100%. P=0 endpoints receive level 0's health
score percent of the traffic, with the rest flowing to P=1 (assuming P=1 is 100% healthy - more on
that later). The integer percents of traffic that each cluster receives are collectively called the
system's "cluster priority load". For instance, for primary cluster, when 20% of P=0 endpoints are
healthy, 20% of P=1 endpoints are healthy, and 10% of P=2 endpoints are healthy; for secondary, when
25% of P=0 endpoints are healthy and 25% of P=1 endpoints are healthy. The primary cluster will
receive 20% * 1.4 + 20% * 1.4 + 10% * 1.4 = 70% of the traffic. The secondary cluster will receive
min(100 - 70, 25% * 1.4 + 25% * 1.4) = 30% of the traffic. The traffic to all clusters sum up to
100. The normalized health score and priority load are pre-computed before selecting the cluster and
priority.

To sum this up in pseudo algorithms:

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

The second tier is delegating the load balancing to the cluster selected in the first step and the
cluster could use any load balancing algorithms specified by :ref:`load balancer type <arch_overview_load_balancing_types>`.
