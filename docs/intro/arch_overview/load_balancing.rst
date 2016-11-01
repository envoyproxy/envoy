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

* Originating/Upstream cluster, Envoy routes requests from originating cluster to upstream cluster
* Local zone, it's the same zone both in originating and upstream cluster
* Zone aware routing, best effort to route requests to local zone in upstream cluster

In deployments where hosts in originating and upstream clusters belong to different zones
Envoy performs zone aware routing.
There are several preconditions when zone aware routing can be performed:

* Both originating and upstream cluster are not in :ref:`panic mode <arch_overview_load_balancing_panic_threshold>`
* Zone aware :ref:`routing is enabled <config_cluster_manager_cluster_runtime_zone_routing>`
* Originating cluster has the same number of zones as upstream one
* Upstream cluster has enough hosts, see :ref:`here <config_cluster_manager_cluster_runtime_zone_routing>` for details.

The purpose of zone aware routing is to send as much traffic to the same zone in upstream cluster as possible keeping
invariant of the same requests per second across all upstream hosts.

Envoy roughly relies on the following algorithm to perform zone aware routing.
Two most important factors are distribution of healthy hosts across zones in originating and upstream cluster.
Envoy tries to push as much traffic as possible to local upstream zone as long as invariant of
roughly same number of requests per hosts in upstream cluster observed. Decision whether Envoy routes to local zone
or perform cross zone request depends mostly on percent of the healthy hosts in the originating cluster
and upstream cluster for local zone.
There are two cases w.r.t. percentage relations between local zone originating and upstream clusters:

* Originating cluster local zone is not smaller than local zone upstream cluster. In this case
  we cannot route all requests from originating cluster to local zone of upstream cluster, because that will
  break invariant of same RPS across all upstream hosts. But we can calculate percentage of requests that can
  be routed directly to the local zone of upstream cluster. The rest of the requests should be routed cross zone,
  specific zone is selected based on the residual capacity of zone (that zone will get some same zone traffic and
  may have additional capacity we can use for cross zone traffic).
* Originating cluster local zone is smaller than local zone upstream cluster. In this case local zone of upstream
  cluster can get all of the requests from originating cluster and also have some space to allow traffic from
  other zones in the originating cluster (if needed).
