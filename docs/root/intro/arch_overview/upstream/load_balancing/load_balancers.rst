.. _arch_overview_load_balancing_types:

Supported load balancers
------------------------

When a filter needs to acquire a connection to a host in an upstream cluster, the cluster manager
uses a load balancing policy to determine which host is selected. The load balancing policies are
pluggable and are specified on a per upstream cluster basis in the :ref:`configuration
<envoy_v3_api_msg_config.cluster.v3.Cluster>`. Note that if no active health checking policy is :ref:`configured
<config_cluster_manager_cluster_hc>` for a cluster, all upstream cluster members are considered
healthy, unless otherwise specified through
:ref:`health_status <envoy_v3_api_field_config.endpoint.v3.LbEndpoint.health_status>`.

.. _arch_overview_load_balancing_types_round_robin:

Weighted round robin
^^^^^^^^^^^^^^^^^^^^

This is a simple policy in which each available upstream host is selected in round
robin order. If :ref:`weights
<envoy_v3_api_field_config.endpoint.v3.LbEndpoint.load_balancing_weight>` are assigned to
endpoints in a locality, then a weighted round robin schedule is used, where
higher weighted endpoints will appear more often in the rotation to achieve the
effective weighting.

.. _arch_overview_load_balancing_types_least_request:

Weighted least request
^^^^^^^^^^^^^^^^^^^^^^

The least request load balancer uses different algorithms depending on whether hosts have the
same or different weights.

* *all weights equal*: An O(1) algorithm which selects N random available hosts as specified in the
  :ref:`configuration <envoy_v3_api_msg_config.cluster.v3.Cluster.LeastRequestLbConfig>` (2 by default) and picks the
  host which has the fewest active requests (`Mitzenmacher et al.
  <https://www.eecs.harvard.edu/~michaelm/postscripts/handbook2001.pdf>`_ has shown that this
  approach is nearly as good as an O(N) full scan). This is also known as P2C (power of two
  choices). The P2C load balancer has the property that host weights will decrease as the number of
  active requests on those hosts increases. P2C selection is particularly useful for load
  balancer implementations due to its resistance to
  `herding behavior <https://en.wikipedia.org/wiki/Thundering_herd_problem>`_.
* *all weights not equal*:  If two or more hosts in the cluster have different load balancing
  weights, the load balancer shifts into a mode where it uses a weighted round robin schedule in
  which weights are dynamically adjusted based on the host's request load at the time of selection.

  In this case the weights are calculated at the time a host is picked using the following formula:

  ``weight = load_balancing_weight / (active_requests + 1)^active_request_bias``.

  :ref:`active_request_bias<envoy_v3_api_field_config.cluster.v3.Cluster.LeastRequestLbConfig.active_request_bias>`
  can be configured via runtime and defaults to 1.0. It must be greater than or equal to 0.0.

  The larger the active request bias is, the more aggressively active requests will lower the
  effective weight.

  If ``active_request_bias`` is set to 0.0, the least request load balancer behaves like the round
  robin load balancer and ignores the active request count at the time of picking.

  For example, if active_request_bias is 1.0, a host with weight 2 and an active request count of 4
  will have an effective weight of 2 / (4 + 1)^1 = 0.4. This algorithm provides good balance at
  steady state but may not adapt to load imbalance as quickly. Additionally, unlike P2C, a host will
  never truly drain, though it will receive fewer requests over time.

.. _arch_overview_load_balancing_types_ring_hash:

Ring hash
^^^^^^^^^

The ring/modulo hash load balancer implements consistent hashing to upstream hosts. Each host is
mapped onto a circle (the "ring") by hashing its address; each request is then routed to a host by
hashing some property of the request, and finding the nearest corresponding host clockwise around
the ring. This technique is also commonly known as `"Ketama" <https://github.com/RJ/ketama>`_
hashing, and like all hash-based load balancers, it is only effective when protocol routing is used
that specifies a value to hash on. If you want something other than the host's address to be used
as the hash key (e.g. the semantic name of your host in a Kubernetes StatefulSet), then you can specify it
in the ``"envoy.lb"`` :ref:`LbEndpoint.Metadata <envoy_v3_api_field_config.endpoint.v3.lbendpoint.metadata>` e.g.:

.. validated-code-block:: yaml
  :type-name: envoy.config.core.v3.Metadata

    filter_metadata:
      envoy.lb:
        hash_key: "YOUR HASH KEY"

This will override :ref:`use_hostname_for_hashing <envoy_v3_api_field_config.cluster.v3.cluster.commonlbconfig.consistent_hashing_lb_config>`.

Each host is hashed and placed on the ring some number of times proportional to its weight. For
example, if host A has a weight of 1 and host B has a weight of 2, then there might be three entries
on the ring: one for host A and two for host B. This doesn't actually provide the desired 2:1
partitioning of the circle, however, since the computed hashes could be coincidentally very close to
one another; so it is necessary to multiply the number of hashes per host---for example inserting
100 entries on the ring for host A and 200 entries for host B---to better approximate the desired
distribution. Best practice is to explicitly set
:ref:`minimum_ring_size<envoy_v3_api_field_config.cluster.v3.Cluster.RingHashLbConfig.minimum_ring_size>` and
:ref:`maximum_ring_size<envoy_v3_api_field_config.cluster.v3.Cluster.RingHashLbConfig.maximum_ring_size>`, and monitor
the :ref:`min_hashes_per_host and max_hashes_per_host
gauges<config_cluster_manager_cluster_stats_ring_hash_lb>` to ensure good distribution. With the
ring partitioned appropriately, the addition or removal of one host from a set of N hosts will
affect only 1/N requests.

When priority based load balancing is in use, the priority level is also chosen by hash, so the
endpoint selected will still be consistent when the set of backends is stable.

.. _arch_overview_load_balancing_types_maglev:

Maglev
^^^^^^

The Maglev load balancer implements consistent hashing to upstream hosts. It uses the algorithm
described in section 3.4 of `this paper <https://static.googleusercontent.com/media/research.google.com/en//pubs/archive/44824.pdf>`_
with a fixed table size of 65537 (see section 5.3 of the same paper). Maglev can be used as a drop
in replacement for the :ref:`ring hash load balancer <arch_overview_load_balancing_types_ring_hash>`
any place in which consistent hashing is desired. Like the ring hash load balancer, a consistent
hashing load balancer is only effective when protocol routing is used that specifies a value to
hash on. If you want something other than the host's address to be used as the hash key (e.g. the
semantic name of your host in a Kubernetes StatefulSet), then you can specify it in the ``"envoy.lb"``
:ref:`LbEndpoint.Metadata <envoy_v3_api_field_config.endpoint.v3.lbendpoint.metadata>` e.g.:

.. validated-code-block:: yaml
  :type-name: envoy.config.core.v3.Metadata

    filter_metadata:
      envoy.lb:
        hash_key: "YOUR HASH KEY"

This will override :ref:`use_hostname_for_hashing <envoy_v3_api_field_config.cluster.v3.cluster.commonlbconfig.consistent_hashing_lb_config>`.

The table construction algorithm places each host in the table some number of times proportional
to its weight, until the table is completely filled. For example, if host A has a weight of 1 and
host B has a weight of 2, then host A will have 21,846 entries and host B will have 43,691 entries
(totaling 65,537 entries). The algorithm attempts to place each host in the table at least once,
regardless of the configured host and locality weights, so in some extreme cases the actual
proportions may differ from the configured weights. For example, if the total number of hosts is
larger than the fixed table size, then some hosts will get 1 entry each and the rest will get 0,
regardless of weight. Best practice is to monitor the :ref:`min_entries_per_host and
max_entries_per_host gauges <config_cluster_manager_cluster_stats_maglev_lb>` to ensure no hosts
are underrepresented or missing.

In general, when compared to the ring hash ("ketama") algorithm, Maglev has substantially faster
table lookup build times as well as host selection times (approximately 10x and 5x respectively
when using a large ring size of 256K entries). While Maglev aims for minimal disruption, it is not
as stable as ring hash when upstream hosts change. More keys will move position when hosts are removed
(simulations show approximately double the keys will move). The amount of disruption can be minimized
by increasing the :ref:`table_size<envoy_v3_api_field_config.cluster.v3.Cluster.MaglevLbConfig.table_size>`.
With that said, for many applications
including Redis, Maglev is very likely a superior drop in replacement for ring hash. The advanced reader can use
:repo:`this benchmark </test/common/upstream/load_balancer_benchmark.cc>` to compare ring hash
versus Maglev with different parameters.

.. _arch_overview_load_balancing_types_random:

Random
^^^^^^

The random load balancer selects a random available host. The random load balancer generally performs
better than round robin if no health checking policy is configured. Random selection avoids bias
towards the host in the set that comes after a failed host.
