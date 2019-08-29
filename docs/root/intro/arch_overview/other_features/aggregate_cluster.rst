.. _arch_overview_aggregate_cluster:

Aggregate Cluster
=================

Aggregate cluster is used for failover between clusters from different service discovery mechanism, e.g., 
failover from EDS upstream cluster to STRICT_DNS upstream cluster. Aggregate cluster loosely couples multiple 
clusters by referring their name in the :ref:`configuration <envoy_api_msg_config.cluster.aggregate.ClusterConfig>`. 
The priority is defined implicitly by the ordering in the :ref:`clusters <envoy_api_field_config.cluster.aggregate.ClusterConfig.clusters>`.
Aggregate cluster uses tiered load balancing. The load balancer chooses a cluster first and then deletgates the load balancing 
to the load balancer of the seleted cluster. The top level load balancer reuse the existing load balancing algorithm by linearizing the 
priority set of multiple clusters into one. 


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

