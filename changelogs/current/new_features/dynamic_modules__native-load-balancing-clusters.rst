Dynamic module clusters (``envoy.clusters.dynamic_modules``) can now use
Envoy's built-in load balancers. In addition to ``CLUSTER_PROVIDED``,
``lb_policy`` may be set to ``LEAST_REQUEST``, ``ROUND_ROBIN``, ``RANDOM``,
``RING_HASH``, or ``MAGLEV``; the module then supplies only host discovery and
Envoy performs host selection.
