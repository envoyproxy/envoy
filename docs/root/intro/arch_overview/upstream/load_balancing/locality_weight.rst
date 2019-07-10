.. _arch_overview_load_balancing_locality_weighted_lb:

Locality weighted load balancing
--------------------------------

One approach to determining how to weight assignments across different zones
and geographical locations is by using explicit weights supplied via EDS in the
:ref:`LocalityLbEndpoints <envoy_api_msg_endpoint.LocalityLbEndpoints>` message.
This approach is mutually exclusive with 
:ref:`zone aware routing <arch_overview_load_balancing_zone_aware_routing>`, since
in the case of locality aware LB, we rely on the management server to provide the
locality weighting, rather than the Envoy-side heuristics used in zone aware
routing.

When all endpoints are available, the locality is picked using a weighted
round-robin schedule, where the locality weight is used for weighting. When some
endpoints in a locality are unavailable, we adjust the locality weight to reflect
this. As with :ref:`priority levels
<arch_overview_load_balancing_priority_levels>`, we assume an
:ref:`over-provision factor <arch_overview_load_balancing_overprovisioning_factor>`
(default value 1.4), which means we do not perform any weight
adjustment when only a small number of endpoints in a locality are unavailable.

Assume a simple set-up with 2 localities X and Y, where X has a locality weight
of 1 and Y has a locality weight of 2, L=Y 100% available,
with default overprovisioning factor 1.4.

+----------------------------+---------------------------+----------------------------+
| L=X healthy endpoints      | Percent of traffic to L=X |  Percent of traffic to L=Y |
+============================+===========================+============================+
| 100%                       | 33%                       |   67%                      |
+----------------------------+---------------------------+----------------------------+
| 70%                        | 33%                       |   67%                      |
+----------------------------+---------------------------+----------------------------+
| 69%                        | 32%                       |   68%                      |
+----------------------------+---------------------------+----------------------------+
| 50%                        | 26%                       |   74%                      |
+----------------------------+---------------------------+----------------------------+
| 25%                        | 15%                       |   85%                      |
+----------------------------+---------------------------+----------------------------+
| 0%                         | 0%                        |   100%                     |
+----------------------------+---------------------------+----------------------------+


To sum this up in pseudo algorithms:

::

  availability(L_X) = 140 * available_X_upstreams / total_X_upstreams
  effective_weight(L_X) = locality_weight_X * min(100, availability(L_X))
  load to L_X = effective_weight(L_X) / Σ_c(effective_weight(L_c))

Note that the locality weighted pick takes place after the priority level is
picked. The load balancer follows these steps:

1. Pick :ref:`priority level <arch_overview_load_balancing_priority_levels>`.
2. Pick locality (as described in this section) within priority level from (1).
3. Pick endpoint using cluster specified load balancer within locality from (2).

Locality weighted load balancing is configured by setting
:ref:`locality_weighted_lb_config
<envoy_api_field_Cluster.CommonLbConfig.locality_weighted_lb_config>` in the
cluster configuration and providing weights in :ref:`LocalityLbEndpoints
<envoy_api_msg_endpoint.LocalityLbEndpoints>` via :ref:`load_balancing_weight
<envoy_api_field_endpoint.LocalityLbEndpoints.load_balancing_weight>`.

By setting :ref:`locality_weight_aware
<Cluster.LbSubsetConfig.locality_weight_aware>`, this feature can be made
compatible with :ref:`load balancer subsetting
<arch_overview_load_balancer_subsets>`. Take care when enabling both load
balancer subsetting and locality weighted load balancing as the resulting
traffic split may be undesirable.

It's possible to smooth out the weighting of a given locality when using
subsets by setting :ref:`scale_locality_weight
<Cluster.LbSubsetConfig.scale_locality_weight>`, which scales the weights by
proportionally to the number of hosts in the subset predicate.
