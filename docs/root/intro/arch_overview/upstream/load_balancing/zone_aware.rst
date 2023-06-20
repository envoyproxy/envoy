.. _arch_overview_load_balancing_zone_aware_routing:

Zone aware routing
------------------

We use the following terminology:

* **Originating/Upstream cluster**: Envoy routes requests from an originating cluster to an upstream
  cluster.
* **Local zone**: The same zone that contains a subset of hosts in both the originating and
  upstream clusters.
* **Zone aware routing**: Best effort routing of requests to an upstream cluster host in the local
  zone.

In deployments where hosts in originating and upstream clusters belong to different zones
Envoy performs zone aware routing. There are several preconditions before zone aware routing can be
performed:

.. _arch_overview_load_balancing_zone_aware_routing_preconditions:

* Both originating and upstream cluster are not in
  :ref:`panic mode <arch_overview_load_balancing_panic_threshold>`.
* Zone aware :ref:`routing is enabled <config_cluster_manager_cluster_runtime_zone_routing>`.
* The originating cluster has the same number of zones as the upstream cluster.
* The upstream cluster has enough hosts. See
  :ref:`here <config_cluster_manager_cluster_runtime_zone_routing>` for more information.

The purpose of zone aware routing is to send as much traffic to the local zone in the upstream
cluster as possible while roughly maintaining the same number of requests per second across all
upstream hosts (depending on load balancing policy).

Envoy tries to push as much traffic as possible to the local upstream zone as long as
roughly the same number of requests per host in the upstream cluster are maintained. The decision of
whether Envoy routes to the local zone or performs cross zone routing depends on the percentage of
healthy hosts in the originating cluster and upstream cluster in the local zone. There are two cases
with regard to percentage relations in the local zone between originating and upstream clusters:

* The originating cluster local zone percentage is greater than the one in the upstream cluster.
  In this case we cannot route all requests from the local zone of the originating cluster to the
  local zone of the upstream cluster because that will lead to request imbalance across all upstream
  hosts. Instead, Envoy calculates the percentage of requests that can be routed directly to the
  local zone of the upstream cluster. The rest of the requests are routed cross zone. The specific
  zone is selected based on the residual capacity of the zone (that zone will get some local zone
  traffic and may have additional capacity Envoy can use for cross zone traffic).
* The originating cluster local zone percentage is smaller than the one in upstream cluster.
  In this case the local zone of the upstream cluster can get all of the requests from the
  local zone of the originating cluster and also have some space to allow traffic from other zones
  in the originating cluster (if needed).

Note that when using multiple priorities, zone aware routing is currently only supported for P=0.

