.. _arch_overview_load_balancing_types:

Supported load balancers
------------------------

When a filter needs to acquire a connection to a host in an upstream cluster, the cluster manager
uses a load balancing policy to determine which host is selected. The load balancing policies are
pluggable and are specified on a per upstream cluster basis in the :ref:`configuration
<envoy_api_msg_Cluster>`. Note that if no active health checking policy is :ref:`configured
<config_cluster_manager_cluster_hc>` for a cluster, all upstream cluster members are considered
healthy.

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

* *all weights 1*: An O(1) algorithm which selects N random healthy hosts as specified in the
  :ref:`configuration <envoy_api_msg_Cluster.LeastRequestLbConfig>` (2 by default) and picks the
  host which has the fewest active requests (`Research
  <http://www.eecs.harvard.edu/~michaelm/postscripts/handbook2001.pdf>`_ has shown that this
  approach is nearly as good as an O(N) full scan). This is also known as P2C (power of two
  choices). The P2C load balancer has the property that a host with the highest number of active
  requests in the cluster will never receive new requests. It will be allowed to drain until it is
  less than or equal to all of the other hosts.
* *not all weights 1*:  If any host in the cluster has a load balancing weight greater than 1, the
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

