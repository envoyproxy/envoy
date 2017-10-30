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

.. _arch_overview_load_balancing_types_original_destination:

Original destination
^^^^^^^^^^^^^^^^^^^^

This is a special purpose load balancer that can only be used with :ref:`an original destination
cluster <arch_overview_service_discovery_types_original_destination>`. Upstream host is selected
based on the downstream connection metadata, i.e., connections are opened to the same address as the
destination address of the incoming connection was before the connection was redirected to
Envoy. New destinations are added to the cluster by the load balancer on-demand, and the cluster
:ref:`periodically <config_cluster_manager_cluster_cleanup_interval_ms>` cleans out unused hosts
from the cluster. No other :ref:`load balancing type <config_cluster_manager_cluster_lb_type>` can
be used with original destination clusters.

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

If subsets are `configured
<https://github.com/envoyproxy/data-plane-api/blob/9897e3f/api/cds.proto#L237>`_ and a route
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
