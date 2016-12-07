.. _config_http_conn_man_route_table_traffic_shifting:

Traffic Shifting
================

Envoy's router can accomplish traffic shifting between routes of a virtual host
based on the :ref:`runtime <config_http_conn_man_route_table_route_runtime>` object in the route configuration.
A common use case is shifting traffic between clusters with different versions of a service deployed on them.

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
                  "key": "routing.traffic_shift.service",
                  "default": 50
                }
              },
              {
                "prefix": "/",
                "cluster": "service_v2",
              }
            ]
          }
        ]
      }
    }

Envoy matches routes with a :ref:`first match <config_http_conn_man_route_table_route_matching>` policy.
If the route has a runtime object, the request will be additionally matched based on the runtime
:ref:`value <config_http_conn_man_route_table_route_runtime_default>`
(or the default, if no value is specified). Thus, by placing routes back-to-back in the above example and specifying
a runtime object in the first route, traffic shifting can be accomplished by changing the runtime value. The flow would
look something like this:

1. Set ``routing.traffic_shift.service`` to ``100``. This would mean that all requests to the ``service`` virtual host
   would match with the v1 route.
2. Set ``routing.traffic_shift.service`` to values ``0 < x < 100``. For instance at ``50``, half of the requests to the
   ``service`` virtual host will not match the v1 route and fall through to the v2 route.
3. Set ``routing.traffic_shift.service`` to ``0``. This means no requests to the ``service`` virtual host will match
   to the v1 route and they will all fall through to the v2 route.
