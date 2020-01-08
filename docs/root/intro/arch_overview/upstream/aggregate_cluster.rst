.. _arch_overview_aggregate_cluster:

Aggregate Cluster
=================

Aggregate cluster is used for failover between clusters with different configuration, e.g., from EDS
upstream cluster to STRICT_DNS upstream cluster, from cluster using ROUND_ROBIN load balancing 
policy to cluster using MAGLEV, from cluster with 0.1s connection timeout to cluster with 1s 
connection timeout, etc. Aggregate cluster loosely couples multiple clusters by referencing their 
name in the :ref:`configuration <envoy_api_msg_config.cluster.aggregate.v2alpha.ClusterConfig>`. The
fallback priority is defined implicitly by the ordering in the :ref:`clusters list <envoy_api_field_config.cluster.aggregate.v2alpha.ClusterConfig.clusters>`.
Aggregate cluster uses tiered load balancing. The load balancer chooses cluster and priority first 
and then delegates the load balancing to the load balancer of the selected cluster. The top level 
load balancer reuses the existing load balancing algorithm by linearizing the priority set of 
multiple clusters into one. 

Linearize Priority Set
----------------------

Upstream hosts are divided into multiple :ref:`priority levels <arch_overview_load_balancing_priority_levels>` 
and each priority level contains a list of healthy, degraded and unhealthy hosts. Linearization is 
used to simplify the host selection during load balancing by merging priority levels from multiple 
clusters. For example, primary cluster has 3 priority levels, secondary has 2 and tertiary has 2 and
the failover ordering is primary, secondary, tertiary. 

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
      "@type": type.googleapis.com/envoy.config.cluster.aggregate.v2alpha.ClusterConfig
      clusters:
      # cluster primary, secondary and tertiary should be defined outside.
      - primary
      - secondary
      - tertiary

Note: :ref:`PriorityLoad retry plugins <envoy_api_field_route.RetryPolicy.retry_priority>` won't 
work for aggregate cluster because the aggregate load balancer will override the *PriorityLoad* 
during load balancing.


Load Balancing Example
----------------------

Aggregate cluster uses tiered load balancing algorithm and the top tier is distributing traffic to 
different clusters according to the health score across all :ref:`priorities <arch_overview_load_balancing_priority_levels>` 
in each cluster. The aggregate cluster in this section includes two clusters which is different from
what the above configuration describes.
 
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

Note: The above load balancing uses default :ref:`overprovisioning factor <arch_overview_load_balancing_overprovisioning_factor>` 
which is 1.4 which means if 80% of the endpoints in a priority level are healthy, that level is 
still considered fully healthy because 80 * 1.4 > 100.

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
