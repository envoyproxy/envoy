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

.. _arch_overview_load_balancing_zone_panic_threshold:

Zone aware routing and local zone panic threshold
-------------------------------------------------

By default Envoy performs zone aware routing where it will send traffic to the same upstream zone.
This feature is in active development.
You can enable/disable it :ref:`via runtime <config_cluster_manager_cluster_runtime>`.
