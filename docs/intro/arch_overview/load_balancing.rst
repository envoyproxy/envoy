.. _arch_overview_load_balancing:

Load balancing
==============

When a filter needs to acquire a connection to a host in an upstream cluster, the cluster manager
uses a load balancing policy to determine which host is selected. The load balancing policies are
pluggable and are specified on a per upstream cluster basis in the :ref:`configuration
<config_cluster_manager_cluster>`. Note that if no active health checking policy is :ref:`configured
<config_cluster_manager_cluster_hc>` for a cluster, all upstream cluster members are considered
healthy.

.. _arch_overview_load_balancing_types:

Supported load balancers
------------------------

Round robin
^^^^^^^^^^^

This is a simple policy in which each healthy upstream host is selected in round robin order.

Weighted least request
^^^^^^^^^^^^^^^^^^^^^^

The least request load balancer uses an O(1) algorithm which selects two random healthy hosts and
picks the host which has fewer active requests. (Research has shown that this approach is nearly as
good as an O(N) full scan). If any host in the cluster has a load balancing weight greater than 1,
the load balancer shifts into a mode where it randomly picks a host and then uses that host <weight>
times. This algorithm is simple and sufficient for load testing. It should not be used where true
weighted least request behavior is desired (generally if request durations are variable and long in
length). We may add a true full scan weighted least request variant in the future to cover this use
case.

Ring hash
^^^^^^^^^

The ring/modulo hash load balancer implements consistent hashing to upstream hosts. The algorithm is
based on mapping all hosts onto a circle such that the addition or removal of a host from the host
set changes only affect 1/N requests. This technique is also commonly known as `"ketama"
<https://github.com/RJ/ketama>`_ hashing. The consistent hashing load balancer is only effective
when protocol routing is used that specifies a value to hash on. Currently the only implemented
mechanism is to hash via :ref:`HTTP header values <config_http_conn_man_route_table_hash_policy>` in
the :ref:`HTTP router filter <arch_overview_http_routing>`. The default minimum ring size is
specified in :ref:`runtime <config_cluster_manager_cluster_runtime_ring_hash>`. The minimum ring
size governs the replication factor for each host in the ring. For example, if the minimum ring
size is 1024 and there are 16 hosts, each host will be replicated 64 times. The ring hash load
balancer does not currently support weighting.

Random
^^^^^^

The random load balancer selects a random healthy host. The random load balancer generally performs
better than round robin if no health checking policy is configured. Random selection avoids bias
towards the host in the set that comes after a failed host.

.. _arch_overview_load_balancing_panic_threshold:

Panic threshold
---------------

During load balancing, Envoy will generally only consider healthy hosts in an upstream cluster.
However, if the percentage of healthy hosts in the cluster becomes too low, Envoy will disregard
health status and balance amongst all hosts. This is known as the *panic threshold*. The default
panic threshold is 50%. This is :ref:`configurable <config_cluster_manager_cluster_runtime>` via
runtime. The panic threshold is used to avoid a situation in which host failures cascade throughout
the cluster as load increases.

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
