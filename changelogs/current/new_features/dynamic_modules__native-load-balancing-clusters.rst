Dynamic module clusters (``envoy.clusters.dynamic_modules``) can now use
Envoy's built-in load balancers. In addition to ``CLUSTER_PROVIDED``,
``lb_policy`` may be set to ``LEAST_REQUEST``, ``ROUND_ROBIN``, or ``RANDOM``;
the module then supplies only host discovery and Envoy performs host selection
(including zone-aware and locality-weighted routing). Thread-aware policies
(ring hash, maglev) are not supported.
