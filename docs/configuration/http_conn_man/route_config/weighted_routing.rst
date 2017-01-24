.. _config_http_conn_man_route_table_weighted_routing:

Traffic splitting across multiple upstreams
===========================================

Envoy's router can split traffic to a route in a virtual host across
multiple upstream clusters. A common use case is A/B testing or
multivariate testing, where two or more versions of the same service are
tested simultaneously. In this case, the traffic to the route has to be
*split* between clusters running different versions of the same
service.

Consider a simple example ``service`` with three versions (v1, v2 and
v3). Envoy provides two ways to split traffic evenly across the three
versions (i.e., ``33%, 33%, 34%``):

.. _config_http_conn_man_route_table_weighted_routing_percentages:

(a) Weight-based cluster selection
----------------------------------

The first option is to use a **single** :ref:`route <config_http_conn_man_route_table_route>` with 
:ref:`weighted_clusters <config_http_conn_man_route_table_route_weighted_clusters>`,
where multiple upstream cluster targets are specified for a single route,
along with weights that indicate the **percentage** of traffic to be sent
to each upstream cluster.

.. code-block:: json

    {
      "route_config": {
        "virtual_hosts": [
          {
            "name": "service",
            "domains": ["*"],
            "routes": [
              {
                "prefix": "/",
                "weighted_clusters": {
                  "clusters" : [
                    { "name" : "service_v1", "weight" : 33 },
                    { "name" : "service_v2", "weight" : 33 },
                    { "name" : "service_v3", "weight" : 34 }
                  ]
                }
              }
            ]
          }
        ]
      }
    }

.. _config_http_conn_man_route_table_weighted_routing_probabilities:

(b) Probabilistic route selection
---------------------------------

The second option is to use **multiple** :ref:`routes <config_http_conn_man_route_table_route>`
with :ref:`runtimes <config_http_conn_man_route_table_route_runtime>` that specify the
**probability** of selecting a route.
Since Envoy matches routes with a :ref:`first match <config_http_conn_man_route_table_route_matching>`
policy, the related routes (one for each upstream cluster) must be placed back-to-back,
along with a runtime in all but the last route.

.. code-block:: json

    {
      "route_config": {
        "virtual_hosts": [
          {
            "name": "service",
            "domains": ["*"],
            "routes": [
              {
                "prefix": "/",
                "cluster": "service_v1",
                "runtime": {
                  "key": "routing.traffic_split.service.service_v1",
                  "default": 33
                }
              },
              {
                "prefix": "/",
                "cluster": "service_v2",
                "runtime": {
                  "key": "routing.traffic_split.service.service_v2",
                  "default": 50
                }
              },
              {
                "prefix": "/",
                "cluster": "service_v3",
              }
            ]
          }
        ]
      }
    }

In the configuration above,

1. ``routing.traffic_split.service.service_v1`` is set to ``33``, so that there is a
   *33\% probability* that the v1 route will be selected by Envoy.
2. ``routing.traffic_split.service.service_v2`` is set to ``50``, so that if the v1 route
   is not selected, between v2 and v3, there is a *50\% probability* that the v2 route will
   be selected by Envoy. If v2 is not selected the traffic falls through to the v3 route.

This distribution of probabilities ensures that the traffic will be split evenly across
all three routes (i.e. ``33%, 33%, 34%``).
