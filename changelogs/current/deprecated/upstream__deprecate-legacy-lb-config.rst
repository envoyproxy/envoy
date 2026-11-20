Deprecated the legacy load balancer configuration fields of the cluster:
:ref:`lb_subset_config <envoy_v3_api_field_config.cluster.v3.Cluster.lb_subset_config>`,
``ring_hash_lb_config``, ``maglev_lb_config``, ``least_request_lb_config``, ``round_robin_lb_config``,
``original_dst_lb_config``, and the ``zone_aware_lb_config``, ``locality_weighted_lb_config`` and
``consistent_hashing_lb_config`` fields of
:ref:`common_lb_config <envoy_v3_api_field_config.cluster.v3.Cluster.common_lb_config>`.
Please configure the load balancer with the corresponding ``envoy.load_balancing_policies.*`` extension
via :ref:`load_balancing_policy <envoy_v3_api_field_config.cluster.v3.Cluster.load_balancing_policy>`.
:ref:`lb_policy <envoy_v3_api_field_config.cluster.v3.Cluster.lb_policy>` is not deprecated and can
still be used to select a load balancer that needs no custom configuration. Configure the original
destination cluster via :ref:`cluster_type <envoy_v3_api_field_config.cluster.v3.Cluster.cluster_type>`.
