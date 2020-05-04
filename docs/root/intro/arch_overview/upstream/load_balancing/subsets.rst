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

If subsets are :ref:`configured <envoy_v3_api_field_config.cluster.v3.Cluster.lb_subset_config>` and a route
specifies no metadata or no subset matching the metadata exists, the subset load balancer initiates
its fallback policy. The default policy is ``NO_FALLBACK``, in which case the request fails as if
the cluster had no hosts. Conversely, the ``ANY_ENDPOINT`` fallback policy load balances across all
hosts in the cluster, without regard to host metadata. Finally, the ``DEFAULT_SUBSET`` causes
fallback to load balance among hosts that match a specific set of metadata. It is possible to
override fallback policy for specific subset selector.

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
supported when hosts are defined using
:ref:`ClusterLoadAssignments <envoy_v3_api_msg_config.endpoint.v3.ClusterLoadAssignment>`. ClusterLoadAssignments are
available via EDS or the Cluster :ref:`load_assignment <envoy_v3_api_field_config.cluster.v3.Cluster.load_assignment>`
field. Host metadata for subset load balancing must be placed under the filter name ``"envoy.lb"``.
Similarly, route metadata match criteria use ``"envoy.lb"`` filter name. Host metadata may be
hierarchical (e.g., the value for a top-level key may be a structured value or list), but the
subset load balancer only compares top-level keys and values. Therefore when using structured
values, a route's match criteria will only match if an identical structured value appears in the
host's metadata.

Finally, note that subset load balancing is not available for the
:ref:`CLUSTER_PROVIDED <envoy_v3_api_enum_value_config.cluster.v3.Cluster.LbPolicy.CLUSTER_PROVIDED>` load balancer
policies.

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
      fallback_policy: NO_FALLBACK

The following table describes some routes and the result of their application to the
cluster. Typically the match criteria would be used with routes matching specific aspects of the
request, such as the path or header information.

======================  =============  ======================================================================
Match Criteria          Balances Over  Reason
======================  =============  ======================================================================
stage: canary           host3          Subset of hosts selected
v: 1.2-pre, stage: dev  host4          Subset of hosts selected
v: 1.0                  host1, host2   Fallback: No subset selector for "v" alone
other: x                host1, host2   Fallback: No subset selector for "other"
(none)                  host1, host2   Fallback: No subset requested
stage: test             empty cluster  As fallback policy is overridden per selector with "NO_FALLBACK" value
======================  =============  ======================================================================

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
