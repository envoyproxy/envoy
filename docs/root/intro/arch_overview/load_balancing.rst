.. _arch_overview_load_balancing:

Load balancing
==============

When a filter needs to acquire a connection to a host in an upstream cluster, the cluster manager
uses a load balancing policy to determine which host is selected. The load balancing policies are
pluggable and are specified on a per upstream cluster basis in the :ref:`configuration
<envoy_api_msg_Cluster>`. Note that if no active health checking policy is :ref:`configured
<config_cluster_manager_cluster_hc>` for a cluster, all upstream cluster members are considered
healthy.

.. _arch_overview_load_balancing_types:

Supported load balancers
------------------------

.. _arch_overview_load_balancing_types_round_robin:

Weighted round robin
^^^^^^^^^^^^^^^^^^^^

This is a simple policy in which each healthy upstream host is selected in round
robin order. If :ref:`weights
<envoy_api_field_endpoint.LbEndpoint.load_balancing_weight>` are assigned to
endpoints in a locality, then a weighted round robin schedule is used, where
higher weighted endpoints will appear more often in the rotation to achieve the
effective weighting.

.. _arch_overview_load_balancing_types_least_request:

Weighted least request
^^^^^^^^^^^^^^^^^^^^^^

The least request load balancer uses different algorithms depending on whether any of the hosts have
weight greater than 1.

* *all weights 1*: An O(1) algorithm which selects two random healthy hosts and
  picks the host which has fewer active requests (`Research
  <http://www.eecs.harvard.edu/~michaelm/postscripts/handbook2001.pdf>`_ has shown that this
  approach is nearly as good as an O(N) full scan). This is also known as P2C (power of two
  choices). The P2C load balancer has the property that a host with the highest number of active
  requests in the cluster will never receive new requests. It will be allowed to drain until it is
  less than or equal to all of the other hosts.
* *all weights not 1*:  If any host in the cluster has a load balancing weight greater than 1, the
  load balancer shifts into a mode where it uses a weighted round robin schedule in which weights
  are dynamically adjusted based on the host's request load at the time of selection (weight is
  divided by the current active request count. For example, a host with weight 2 and an active
  request count of 4 will have a synthetic weight of 2 / 4 = 0.5). This algorithm provides good
  balance at steady state but may not adapt to load imbalance as quickly. Additionally, unlike P2C,
  a host will never truly drain, though it will receive fewer requests over time.

  .. note::
    If all weights are not 1, but are the same (e.g., 42), Envoy will still use the weighted round
    robin schedule instead of P2C.

.. _arch_overview_load_balancing_types_ring_hash:

Ring hash
^^^^^^^^^

The ring/modulo hash load balancer implements consistent hashing to upstream hosts. The algorithm is
based on mapping all hosts onto a circle such that the addition or removal of a host from the host
set changes only affect 1/N requests. This technique is also commonly known as `"ketama"
<https://github.com/RJ/ketama>`_ hashing. A consistent hashing load balancer is only effective
when protocol routing is used that specifies a value to hash on. The minimum ring size governs the
replication factor for each host in the ring. For example, if the minimum ring size is 1024 and
there are 16 hosts, each host will be replicated 64 times. The ring hash load balancer does not
currently support weighting.

When priority based load balancing is in use, the priority level is also chosen by hash, so the
endpoint selected will still be consistent when the set of backends is stable.

.. note::

  The ring hash load balancer does not support :ref:`locality weighted load
  balancing <arch_overview_load_balancing_locality_weighted_lb>`.

.. _arch_overview_load_balancing_types_maglev:

Maglev
^^^^^^

The Maglev load balancer implements consistent hashing to upstream hosts. It uses the algorithm
described in section 3.4 of `this paper <https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf>`_
with a fixed table size of 65537 (see section 5.3 of the same paper). Maglev can be used as a drop
in replacement for the :ref:`ring hash load balancer <arch_overview_load_balancing_types_ring_hash>`
any place in which consistent hashing is desired. Like the ring hash load balancer, a consistent
hashing load balancer is only effective when protocol routing is used that specifies a value to
hash on.

In general, when compared to the ring hash ("ketama") algorithm, Maglev has substantially faster
table lookup build times as well as host selection times (approximately 10x and 5x respectively
when using a large ring size of 256K entries). The downside of Maglev is that it is not as stable
as ring hash. More keys will move position when hosts are removed (simulations show approximately
double the keys will move). With that said, for many applications including Redis, Maglev is very
likely a superior drop in replacement for ring hash. The advanced reader can use
:repo:`this benchmark </test/common/upstream/load_balancer_benchmark.cc>` to compare ring hash
versus Maglev with different parameters.


.. _arch_overview_load_balancing_types_random:

Random
^^^^^^

The random load balancer selects a random healthy host. The random load balancer generally performs
better than round robin if no health checking policy is configured. Random selection avoids bias
towards the host in the set that comes after a failed host.

.. _arch_overview_load_balancing_types_original_destination:

Original destination
--------------------

This is a special purpose load balancer that can only be used with :ref:`an original destination
cluster <arch_overview_service_discovery_types_original_destination>`. Upstream host is selected
based on the downstream connection metadata, i.e., connections are opened to the same address as the
destination address of the incoming connection was before the connection was redirected to
Envoy. New destinations are added to the cluster by the load balancer on-demand, and the cluster
:ref:`periodically <envoy_api_field_Cluster.cleanup_interval>` cleans out unused hosts
from the cluster. No other :ref:`load balancing policy <envoy_api_field_Cluster.lb_policy>` can
be used with original destination clusters.

.. _arch_overview_load_balancing_types_original_destination_request_header:

Original destination host request header
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Envoy can also pick up the original destination from a HTTP header called
:ref:`x-envoy-orignal-dst-host <config_http_conn_man_headers_x-envoy-original-dst-host>`.
Please note that fully resolved IP address should be passed in this header. For example if a request has to be
routed to a host with IP address 10.195.16.237 at port 8888, the request header value should be set as
``10.195.16.237:8888``.

.. _arch_overview_load_balancing_overprovisioning_factor:

Overprovisioning Factor
-----------------------
Priority levels and localities are considered overprovisioned with
:ref:`this percentage <envoy_api_field_ClusterLoadAssignment.Policy.overprovisioning_factor>`.
Envoy doesn't consider a priority level or locality unhealthy until the
percentage of healthy hosts multiplied by the overprovisioning factor drops
below 100. The default value is 1.4, so a priority level or locality will not be
considered unhealthy until the percentage of healthy endpoints goes below 72%.

.. _arch_overview_load_balancing_priority_levels:

Priority levels
------------------

During load balancing, Envoy will generally only consider hosts configured at the highest priority
level. For each EDS :ref:`LocalityLbEndpoints<envoy_api_msg_endpoint.LocalityLbEndpoints>` an optional
priority may also be specified. When endpoints at the highest priority level (P=0) are healthy, all
traffic will land on endpoints in that priority level. As endpoints for the highest priority level
become unhealthy, traffic will begin to trickle to lower priority levels.

Currently, it is assumed that each priority level is over-provisioned by the
:ref:`overprovisioning factor <arch_overview_load_balancing_overprovisioning_factor>`.
With default factor value 1.4, if 80% of the endpoints are healthy, the priority level is still considered
healthy because 80*1.4 > 100. As the number of healthy endpoints dips below 72%, the health of the priority level
goes below 100. At that point the percent of traffic equivalent to the health of P=0 will go to P=0
and remaining traffic will flow to P=1.

It is important to understand how Envoy evaluates priority levels' health. Each priority level is assigned
a health value which basically is a percentage of healthy hosts in relation to total number of hosts in a given 
priority level multiplied by overprovisioning factor of 1.4. 
A priority level's health is integer value between 0% and 100%. When there are more than one priority levels
in a cluster, Envoy adds all priority levels' health values and caps it at 100%. This is called normalized total health. Value 0% of 
normalized total health means that all hosts in all priority levels are unhealthy. Value 100% of normalized total health may 
describe many situations: all levels have health of 100% or 4 levels have health value of 30% each.
When normalized total health value is 100%, Envoy assumes that there are enough healthy hosts in all priority 
levels to handle the load. Not all hosts need to be in one priority as Envoy distributes traffic across priority 
levels based on each priority level's health value.
In order for load distribution algorithm and normalized total health calculation to work properly, the number of hosts
in each priority level should be close. Envoy assumes that for example 100% healthy priority level P=1 is able to take
the entire traffic from P=0 should all its hosts become unhealthy. If P=0 has 10 hosts and P=1 has only 2 hosts, P=1 may be unable
to take the entire load from P=0, even though P=1 health is 100%.

Assume a simple set-up with 2 priority levels, P=1 100% healthy.

+----------------------------+----------------+-----------------+
| P=0 healthy endpoints      | Traffic to P=0 |  Traffic to P=1 |
+============================+================+=================+
| 100%                       | 100%           |   0%            |
+----------------------------+----------------+-----------------+
| 72%                        | 100%           |   0%            |
+----------------------------+----------------+-----------------+
| 71%                        | 99%            |   1%            |
+----------------------------+----------------+-----------------+
| 50%                        | 70%            |   30%           |
+----------------------------+----------------+-----------------+
| 25%                        | 35%            |   65%           |
+----------------------------+----------------+-----------------+
| 0%                         | 0%             |   100%          |
+----------------------------+----------------+-----------------+

If P=1 becomes unhealthy, it will continue to take spilled load from P=0 until the sum of the health
P=0 + P=1 goes below 100. At this point the healths will be scaled up to an "effective" health of
100%.

+------------------------+-------------------------+-----------------+----------------+
| P=0 healthy endpoints  | P=1 healthy endpoints   | Traffic to  P=0 | Traffic to P=1 |
+========================+=========================+=================+================+
| 100%                   |  100%                   | 100%            |   0%           |
+------------------------+-------------------------+-----------------+----------------+
| 72%                    |  72%                    | 100%            |   0%           |
+------------------------+-------------------------+-----------------+----------------+
| 71%                    |  71%                    | 99%             |   1%           |
+------------------------+-------------------------+-----------------+----------------+
| 50%                    |  50%                    | 70%             |   30%          |
+------------------------+-------------------------+-----------------+----------------+
| 25%                    |  100%                   | 35%             |   65%          |
+------------------------+-------------------------+-----------------+----------------+
| 25%                    |  25%                    | 50%             |   50%          |
+------------------------+-------------------------+-----------------+----------------+

As more priorities are added, each level consumes load equal to its "scaled" effective health, so
P=2 would only receive traffic if the combined health of P=0 + P=1 was less than 100.

+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| P=0 healthy endpoints | P=1 healthy endpoints | P=2 healthy endpoints | Traffic to P=0 | Traffic to P=1 | Traffic to P=2 |
+=======================+=======================+=======================+================+================+================+
| 100%                  |  100%                 |  100%                 | 100%           |   0%           |   0%           |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 72%                   |  72%                  |  100%                 | 100%           |   0%           |   0%           |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 71%                   |  71%                  |  100%                 | 99%            |   1%           |   0%           |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 50%                   |  50%                  |  100%                 | 70%            |   30%          |   0%           |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 25%                   |  100%                 |  100%                 | 35%            |   65%          |   0%           |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+
| 25%                   |  25%                  |  100%                 | 25%            |   25%          |   50%          |
+-----------------------+-----------------------+-----------------------+----------------+----------------+----------------+

To sum this up in pseudo algorithms:

::

  load to P_0 = min(100, health(P_0) * 100 / normalized_total_health)
  health(P_X) = 140 * healthy_P_X_backends / total_P_X_backends
  normalized_total_health = min(100, Σ(health(P_0)...health(P_X))
  load to P_X = 100 - Σ(percent_load(P_0)..percent_load(P_X-1))

.. _arch_overview_load_balancing_panic_threshold:

Panic threshold
---------------

During load balancing, Envoy will generally only consider healthy hosts in an upstream cluster.
However, if the percentage of healthy hosts in the cluster becomes too low, Envoy will disregard
health status and balance amongst all hosts. This is known as the *panic threshold*. The default
panic threshold is 50%. This is :ref:`configurable <config_cluster_manager_cluster_runtime>` via
runtime as well as in the :ref:`cluster configuration
<envoy_api_field_Cluster.CommonLbConfig.healthy_panic_threshold>`. The panic threshold
is used to avoid a situation in which host failures cascade throughout the cluster as load
increases.

Panic thresholds work in conjunction with priorities. If number of healthy hosts in a given priority
goes down, Envoy will try to shift some traffic to lower priorities. If it succeeds in finding enough 
healthy hosts in lower priorities, Envoy will disregard panic thresholds. In mathematical terms, 
if normalized total health across all priority levels is 100%, Envoy disregards panic thresholds but continues to
distribute traffic load across priorities according to algorithm described :ref:`here <arch_overview_load_balancing_priority_levels>`. 
However, when value of normalized total health drops below 100%, Envoy assumes that there is not enough healthy
hosts across all priority levels. It continues to distribute traffic load across priorities, but if a specific priority level's
health is below panic threshold, traffic will go to all hosts in that priority regardless of their health.

The following examples explain relationship between normalized total health and panic threshold. It is 
assumed that default value of 50% is used for panic threshold.

Assume a simple set-up with 2 priority levels, P=1 100% healthy. In this scenario
normalized total health is always 100% and P=0 never enters panic mode and Envoy is able to shift entire traffic to P=1.

+-------------+------------+--------------+------------+--------------+--------------+
| P=0 healthy | Traffic    | P=0 in panic | Traffic    | P=1 in panic | normalized   |
| endpoints   |  to P=0    |              | to P=1     |              | total health |
+=============+============+==============+============+==============+==============+
| 100%        |  100%      | NO           |   0%       | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+
| 72%         |  100%      | NO           |   0%       | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+
| 71%         |  100%      | NO           |   0%       | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+
| 50%         |  100%      | NO           |   0%       | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+
| 25%         |  100%      | NO           |   0%       | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+
| 0%          |  100%      | NO           |   0%       | NO           |  100%        |
+-------------+------------+--------------+------------+--------------+--------------+

If P=1 becomes unhealthy, panic threshold continues to be disregarded until the sum of the health
P=0 + P=1 goes below 100%. At this point Envoy starts checking panic threshold value for each 
priority.

+-------------+-------------+----------+--------------+----------+--------------+-------------+
| P=0 healthy | P=1 healthy | Traffic  | P=0 in panic | Traffic  | P=1 in panic | normalized  |
| endpoints   | endpoints   | to P=0   |              | to P=1   |              | total health|
+=============+=============+==========+==============+==========+==============+=============+
| 100%        |  100%       |  100%    | NO           |   0%     | NO           |  100%       |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 72%         |  72%        |  100%    | NO           |   0%     | NO           |  100%       |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 71%         |  71%        |  99%     | NO           |   1%     | NO           |  100%       |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 50%         |  60%        |  50%     | NO           |   50%    | NO           |  100%       |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 25%         |  100%       |  25%     | NO           |   75%    | NO           |  100%       |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 25%         |  25%        |  50%     | YES          |   50%    | YES          |  70%        |
+-------------+-------------+----------+--------------+----------+--------------+-------------+
| 5%          |  65%        |  7%      | YES          |   93%    | NO           |  98%        |
+-------------+-------------+----------+--------------+----------+--------------+-------------+

Note that panic thresholds can be configured *per-priority*.

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

.. _arch_overview_load_balancing_locality_weighted_lb:

Locality weighted load balancing
--------------------------------

Another approach to determining how to weight assignments across different zones
and geographical locations is by using explicit weights supplied via EDS in the
:ref:`LocalityLbEndpoints <envoy_api_msg_endpoint.LocalityLbEndpoints>` message.
This approach is mutually exclusive with the above zone aware routing, since in
the case of locality aware LB, we rely on the management server to provide the
locality weighting, rather than the Envoy-side heuristics used in zone aware
routing.

When all endpoints are healthy, the locality is picked using a weighted
round-robin schedule, where the locality weight is used for weighting. When some
endpoints in a locality are unhealthy, we adjust the locality weight to reflect
this. As with :ref:`priority levels
<arch_overview_load_balancing_priority_levels>`, we assume an
:ref:`over-provision factor <arch_overview_load_balancing_overprovisioning_factor>`
(default value 1.4), which means we do not perform any weight
adjustment when only a small number of endpoints in a locality are unhealthy.

Assume a simple set-up with 2 localities X and Y, where X has a locality weight
of 1 and Y has a locality weight of 2, L=Y 100% healthy,
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

  health(L_X) = 140 * healthy_X_backends / total_X_backends
  effective_weight(L_X) = locality_weight_X * min(100, health(L_X))
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

This feature is not compatible with :ref:`load balancer subsetting
<arch_overview_load_balancer_subsets>`, since it is not straightforward to
reconcile locality level weighting with sensible weights for individual subsets.

.. _arch_overview_load_balancer_subsets:

Load Balancer Subsets
---------------------

Envoy may be configured to divide hosts within an upstream cluster into subsets based on metadata
attached to the hosts. Routes may then specify the metadata that a host must match in order to be
selected by the load balancer, with the option of falling back to a predefined set of hosts,
including any host.

Subsets use the load balancer policy specified by the cluster. The original destination policy may
not be used with subsets because the upstream hosts are not known in advance. Subsets are compatible
with zone aware routing, but be aware that the use of subsets may easily violate the minimum hosts
condition described above.

If subsets are :ref:`configured <envoy_api_field_Cluster.lb_subset_config>` and a route
specifies no metadata or no subset matching the metadata exists, the subset load balancer initiates
its fallback policy. The default policy is ``NO_ENDPOINT``, in which case the request fails as if
the cluster had no hosts. Conversely, the ``ANY_ENDPOINT`` fallback policy load balances across all
hosts in the cluster, without regard to host metadata. Finally, the ``DEFAULT_SUBSET`` causes
fallback to load balance among hosts that match a specific set of metadata.

Subsets must be predefined to allow the subset load balancer to efficiently select the correct
subset of hosts. Each definition is a set of keys, which translates to zero or more
subsets. Conceptually, each host that has a metadata value for all of the keys in a definition is
added to a subset specific to its key-value pairs. If no host has all the keys, no subsets result
from the definition. Multiple definitions may be provided, and a single host may appear in multiple
subsets if it matches multiple definitions.

During routing, the route's metadata match configuration is used to find a specific subset. If there
is a subset with the exact keys and values specified by the route, the subset is used for load
balancing. Otherwise, the fallback policy is used. The cluster's subset configuration must,
therefore, contain a definition that has the same keys as a given route in order for subset load
balancing to occur.

This feature can only be enabled using the V2 configuration API. Furthermore, host metadata is only
supported when using the EDS discovery type for clusters. Host metadata for subset load balancing
must be placed under the filter name ``"envoy.lb"``. Similarly, route metadata match criteria use
the ``"envoy.lb"`` filter name. Host metadata may be hierarchical (e.g., the value for a top-level
key may be a structured value or list), but the subset load balancer only compares top-level keys
and values. Therefore when using structured values, a route's match criteria will only match if an
identical structured value appears in the host's metadata.

Examples
^^^^^^^^

We'll use simple metadata where all values are strings. Assume the following hosts are defined and
associated with a cluster:

======  ======================
Host    Metadata
======  ======================
host1   v: 1.0, stage: prod
host2   v: 1.0, stage: prod
host3   v: 1.1, stage: canary
host4   v: 1.2-pre, stage: dev
======  ======================

The cluster may enable subset load balancing like this:

::

  ---
  name: cluster-name
  type: EDS
  eds_cluster_config:
    eds_config:
      path: '.../eds.conf'
  connect_timeout:
    seconds: 10
  lb_policy: LEAST_REQUEST
  lb_subset_config:
    fallback_policy: DEFAULT_SUBSET
    default_subset:
      stage: prod
    subset_selectors:
    - keys:
      - v
      - stage
    - keys:
      - stage

The following table describes some routes and the result of their application to the
cluster. Typically the match criteria would be used with routes matching specific aspects of the
request, such as the path or header information.

======================  =============  ==========================================
Match Criteria          Balances Over  Reason
======================  =============  ==========================================
stage: canary           host3          Subset of hosts selected
v: 1.2-pre, stage: dev  host4          Subset of hosts selected
v: 1.0                  host1, host2   Fallback: No subset selector for "v" alone
other: x                host1, host2   Fallback: No subset selector for "other"
(none)                  host1, host2   Fallback: No subset requested
======================  =============  ==========================================

Metadata match criteria may also be specified on a route's weighted clusters. Metadata match
criteria from the selected weighted cluster are merged with and override the criteria from the
route:

====================  ===============================  ====================
Route Match Criteria  Weighted Cluster Match Criteria  Final Match Criteria
====================  ===============================  ====================
stage: canary         stage: prod                      stage: prod
v: 1.0                stage: prod                      v: 1.0, stage: prod
v: 1.0, stage: prod   stage: canary                    v: 1.0, stage: canary
v: 1.0, stage: prod   v: 1.1, stage: canary            v: 1.1, stage: canary
(none)                v: 1.0                           v: 1.0
v: 1.0                (none)                           v: 1.0
====================  ===============================  ====================


Example Host With Metadata
**************************

An EDS ``LbEndpoint`` with host metadata:

::

  ---
  endpoint:
    address:
      socket_address:
        protocol: TCP
        address: 127.0.0.1
        port_value: 8888
  metadata:
    filter_metadata:
      envoy.lb:
        version: '1.0'
        stage: 'prod'


Example Route With Metadata Criteria
************************************

An RDS ``Route`` with metadata match criteria:

::

  ---
  match:
    prefix: /
  route:
    cluster: cluster-name
    metadata_match:
      filter_metadata:
        envoy.lb:
          version: '1.0'
          stage: 'prod'
