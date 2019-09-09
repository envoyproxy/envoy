.. _arch_overview_aggregate_cluster:

Aggregate Cluster
=================

Aggregate cluster is used for failover between clusters from different service discovery mechanism, e.g., 
failover from EDS upstream cluster to STRICT_DNS upstream cluster. Aggregate cluster loosely couples multiple 
clusters by referencing their name in the :ref:`configuration <envoy_api_msg_config.cluster.aggregate.ClusterConfig>`. 
The fallback priority is defined implicitly by the ordering in the :ref:`clusters <envoy_api_field_config.cluster.aggregate.ClusterConfig.clusters>`.
Aggregate cluster uses tiered load balancing. The load balancer chooses cluster and piority first and then deletgates the load balancing from that priority
to the load balancer of the seleted cluster. The top level load balancer reuse the existing load balancing algorithm by linearizing the 
priority set of multiple clusters into one. 

Linearize Priority Set
----------------------

Upstream hosts are divided into multiple :ref:`priority levels <arch_overview_load_balancing_priority_levels>` and each priority level contains 
a list of healthy, degraded and unhealthy hosts. Linearization is used to simplify the host selection during load balancing by merging priority levels 
from multiple clusters. For example, primary cluster has 3 priority levels, secondary has 2 and tertiary has 2 and the failover ordering is 
primary, secondary, tertiary.

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
| Tertiary  | 2              |  6                                  |
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
      "@type": type.googleapis.com/envoy.config.cluster.aggregate.ClusterConfig
      clusters:
      # cluster primary, secondary and tertiary should be defined outside.
      - primary
      - secondary
      - tertiary

Note: PriorityLoad retry plugins won't work for aggregate cluster because the aggregate load balancer
will override the PriorityLoad during load balancing.