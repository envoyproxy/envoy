.. _config_http_conn_man_route_table_traffic_splitting:

Traffic Shifting/Splitting
===========================================

.. contents::
  :local:

Envoy's router can split traffic to a route in a virtual host across
two or more upstream clusters. There are two common use cases.

1. Version upgrades: traffic to a route is shifted gradually
from one cluster to another. The
:ref:`traffic shifting <config_http_conn_man_route_table_traffic_splitting_shift>`
section describes this scenario in more detail.

2. A/B testing or multivariate testing: ``two or more versions`` of
the same service are tested simultaneously. The traffic to the route has to
be *split* between clusters running different versions of the same
service. The
:ref:`traffic splitting <config_http_conn_man_route_table_traffic_splitting_split>`
section describes this scenario in more detail.

.. _config_http_conn_man_route_table_traffic_splitting_shift:

Traffic shifting between two upstreams
--------------------------------------

The :ref:`runtime <envoy_api_field_route.RouteMatch.runtime_fraction>` object
in the route configuration determines the probability of selecting a
particular route (and hence its cluster). By using the *runtime_fraction*
configuration, traffic to a particular route in a virtual host can be
gradually shifted from one cluster to another. Consider the following
example configuration, where two versions ``helloworld_v1`` and
``helloworld_v2`` of a service named ``helloworld`` are declared in the
envoy configuration file.

.. code-block:: yaml

  virtual_hosts:
     - name: www2
       domains:
       - '*'
       routes:
         - match:
             prefix: /
             runtime_fraction:
               default_value:
                 numerator: 50
                 denominator: HUNDRED
               runtime_key: routing.traffic_shift.helloworld
           route:
             cluster: helloworld_v1
         - match:
             prefix: /
           route:
             cluster: helloworld_v2


Envoy matches routes with a :ref:`first match <config_http_conn_man_route_table_route_matching>` policy.
If the route has a runtime_fraction object, the request will be additionally matched based on the runtime_fraction
:ref:`value <envoy_api_field_route.RouteMatch.runtime_fraction>`
(or the default, if no value is specified). Thus, by placing routes
back-to-back in the above example and specifying a runtime_fraction object in the
first route, traffic shifting can be accomplished by changing the runtime_fraction
value. The following are the approximate sequence of actions required to
accomplish the task.

1. In the beginning, set ``routing.traffic_shift.helloworld`` to ``100``,
   so that all requests to the ``helloworld`` virtual host would match with
   the v1 route and be served by the ``helloworld_v1`` cluster.
2. To start shifting traffic to ``helloworld_v2`` cluster, set
   ``routing.traffic_shift.helloworld`` to values ``0 < x < 100``. For
   instance at ``90``, 1 out of every 10 requests to the ``helloworld``
   virtual host will not match the v1 route and will fall through to the v2
   route.
3. Gradually decrease the value set in ``routing.traffic_shift.helloworld``
   so that a larger percentage of requests match the v2 route.
4. When ``routing.traffic_shift.helloworld`` is set to ``0``, no requests
   to the ``helloworld`` virtual host will match to the v1 route. All
   traffic would now fall through to the v2 route and be served by the
   ``helloworld_v2`` cluster.


.. _config_http_conn_man_route_table_traffic_splitting_split:

Traffic splitting across multiple upstreams
-------------------------------------------

Consider the ``helloworld`` example again, now with three versions (v1, v2 and
v3) instead of two. To split traffic evenly across the three versions
(i.e., ``33%, 33%, 34%``), the ``weighted_clusters`` option can be used to
specify the weight for each upstream cluster.

Unlike the previous example, a **single** :ref:`route
<envoy_api_msg_route.Route>` entry is sufficient. The
:ref:`weighted_clusters <envoy_api_field_route.RouteAction.weighted_clusters>`
configuration block in a route can be used to specify multiple upstream clusters
along with weights that indicate the **percentage** of traffic to be sent
to each upstream cluster.

.. code-block:: yaml

  virtual_hosts:
     - name: www2
       domains:
       - '*'
       routes:
         - match: { prefix: / }
           route:
             weighted_clusters:
               clusters:
                 - name: helloworld_v1
                   weight: 33
                 - name: helloworld_v2
                   weight: 33
                 - name: helloworld_v3
                   weight: 34


By default, the weights must sum to exactly 100. In the V2 API, the
:ref:`total weight <envoy_api_field_route.WeightedCluster.total_weight>` defaults to 100, but can
be modified to allow finer granularity.

The weights assigned to each cluster can be dynamically adjusted using the
following runtime variables: ``routing.traffic_split.helloworld.helloworld_v1``,
``routing.traffic_split.helloworld.helloworld_v2`` and
``routing.traffic_split.helloworld.helloworld_v3``.
